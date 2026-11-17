#include <winsock2.h>
#include <ws2tcpip.h>
#include "StreamSocket.h"
#include <wincrypt.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct InternetHandle
{
    HINTERNET value{};
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
struct TcpSocket
{
    SOCKET value{INVALID_SOCKET};
    ~TcpSocket() { if (value != INVALID_SOCKET) closesocket(value); }
};
struct Connection
{
    InternetHandle session, connection, request;
    std::unique_ptr<cryptoprice::StreamSocket> socket;
};
std::unique_ptr<Connection> Connect(unsigned short port)
{
    auto result = std::make_unique<Connection>();
    result->session.value = WinHttpOpen(L"CryptoPrice-Socket-Test", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    Check(result->session.value != nullptr, "WinHttpOpen");
    Check(WinHttpSetTimeouts(result->session.value, 2000, 2000, 2000, 2000), "WinHttpSetTimeouts");
    result->connection.value = WinHttpConnect(result->session.value, L"127.0.0.1", port, 0);
    Check(result->connection.value != nullptr, "WinHttpConnect");
    result->request.value = WinHttpOpenRequest(result->connection.value, L"GET", L"/", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    Check(result->request.value != nullptr, "WinHttpOpenRequest");
    Check(WinHttpSetOption(result->request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0), "Upgrade option");
    Check(WinHttpSendRequest(result->request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0), "WinHttpSendRequest");
    Check(WinHttpReceiveResponse(result->request.value, nullptr), "WinHttpReceiveResponse");
    auto upgraded = WinHttpWebSocketCompleteUpgrade(result->request.value, 0);
    Check(upgraded != nullptr, "Complete upgrade");
    result->socket = std::make_unique<cryptoprice::StreamSocket>(upgraded);
    DWORD timeout = 200;
    Check(WinHttpSetOption(upgraded, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &timeout, sizeof(timeout)), "Close timeout");
    return result;
}
void Handshake(SOCKET peer)
{
    DWORD timeout = 2000;
    Check(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0,
          "Peer timeout");
    std::string headers;
    while (headers.find("\r\n\r\n") == std::string::npos)
    {
        char buffer[1024];
        const int count = recv(peer, buffer, sizeof(buffer), 0);
        Check(count > 0 && headers.size() < 16384, "Read upgrade request");
        headers.append(buffer, count);
    }
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto start = lower.find("sec-websocket-key:");
    Check(start != std::string::npos, "WebSocket key");
    start += std::string("sec-websocket-key:").size();
    while (headers[start] == ' ') ++start;
    const auto end = headers.find("\r\n", start);
    const std::string key = headers.substr(start, end - start) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    BYTE hash[20]; DWORD hashSize = sizeof(hash);
    Check(CryptHashCertificate(0, CALG_SHA1, 0, reinterpret_cast<const BYTE*>(key.data()),
                              static_cast<DWORD>(key.size()), hash, &hashSize), "SHA1");
    char accept[64]; DWORD acceptSize = sizeof(accept);
    Check(CryptBinaryToStringA(hash, hashSize, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, accept, &acceptSize), "Base64");
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                                std::string(accept) + "\r\n\r\n";
    std::size_t sent = 0;
    while (sent < response.size())
    {
        const int count = send(peer, response.data() + sent, static_cast<int>(response.size() - sent), 0);
        Check(count > 0, "Send upgrade response");
        sent += count;
    }
}
void Run()
{
    TcpSocket listener;
    listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Check(listener.value != INVALID_SOCKET, "Listen socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(bind(listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "Bind loopback");
    Check(listen(listener.value, 1) == 0, "Listen");
    int addressSize = sizeof(address);
    Check(getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0, "Local port");

    for (int attempt = 0; attempt < 5; ++attempt)
    {
        auto connecting = std::async(std::launch::async, Connect, ntohs(address.sin_port));
        fd_set readable;
        FD_ZERO(&readable); FD_SET(listener.value, &readable);
        timeval wait{3, 0};
        Check(select(0, &readable, nullptr, nullptr, &wait) > 0, "Accept deadline");
        TcpSocket peer;
        peer.value = accept(listener.value, nullptr, nullptr);
        Check(peer.value != INVALID_SOCKET, "Accept");
        Handshake(peer.value);
        auto connection = connecting.get();
        auto& stream = *connection->socket;
        std::promise<void> started;
        auto startedFuture = started.get_future();
        auto receiving = std::async(std::launch::async, [&] {
            char buffer[32]; DWORD bytes{};
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            started.set_value();
            return stream.Receive(buffer, sizeof(buffer), bytes, type);
        });
        startedFuture.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto beforeClose = std::chrono::steady_clock::now();
        // Peer deliberately never replies to the close handshake.
        const DWORD closeResult = stream.Interrupt();
        Check(receiving.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "Pending receive was not cancelled");
        const auto receiveResult = receiving.get();
        Check(receiveResult == ERROR_WINHTTP_OPERATION_CANCELLED, "Unexpected receive cancellation result");
        Check(closeResult != ERROR_INVALID_OPERATION && closeResult != ERROR_INVALID_PARAMETER, "Invalid close operation");
        Check(std::chrono::steady_clock::now() - beforeClose < std::chrono::seconds(2), "Close handshake was not bounded");
        Check(stream.Interrupt() == NO_ERROR, "Repeated interrupt is not idempotent");
        bool sendRejected{};
        try { stream.Send("ping"); } catch (const std::exception&) { sendRejected = true; }
        Check(sendRejected, "Send after interrupt must be rejected");
        char buffer[1]; DWORD bytes{}; WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        Check(stream.Receive(buffer, sizeof(buffer), bytes, type) == ERROR_WINHTTP_OPERATION_CANCELLED,
              "Receive after interrupt must be rejected");
    }
}
}
int main()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
    int result = 0;
    try { Run(); std::cout << "Stream socket cancellation tests passed\n"; }
    catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; result = 1; }
    WSACleanup();
    return result;
}
