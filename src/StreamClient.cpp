#include "StreamClient.h"
#include "StreamProtocol.h"
#include "MarketClient.h"
#include "Module.h"

#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <stdexcept>
#include <set>
#include <thread>

namespace cryptoprice
{
std::atomic<bool> g_processStopping{false};
namespace
{
struct Handle
{
    HINTERNET value{};
    explicit Handle(HINTERNET h = nullptr) : value(h) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::string Utf8(const std::wstring& text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(size), '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size, nullptr, nullptr);
    return output;
}
std::runtime_error NetworkError(const char* operation)
{
    return std::runtime_error(std::string(operation) + ": Windows error " + std::to_string(GetLastError()));
}
struct Socket
{
    std::mutex mutex;
    HINTERNET value{};
    void Close()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (value) { WinHttpCloseHandle(value); value = nullptr; }
    }
    void Send(const std::string& text)
    {
        if (text.empty()) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (!value) throw std::runtime_error("WebSocket closed");
        const DWORD error = WinHttpWebSocketSend(value, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                                const_cast<char*>(text.data()), static_cast<DWORD>(text.size()));
        if (error != NO_ERROR) throw std::runtime_error("WebSocket send error " + std::to_string(error));
    }
    ~Socket() { Close(); }
};
}
struct StreamClient::Session : std::enable_shared_from_this<Session>
{
    std::atomic<bool> stop{false};
    std::shared_ptr<std::atomic<bool>> closing;
    unsigned long long generation{};
    Callback callback;
    // Serializes stop vs callbacks so old generations cannot call a destroyed owner.
    std::mutex callbackMutex;
    std::mutex waitMutex;
    std::condition_variable wake;
    bool Stopped() const { return stop.load() || closing->load() || g_processStopping.load(); }
    void Cancel()
    {
        { std::lock_guard<std::mutex> lock(callbackMutex); stop.store(true); }
        wake.notify_all();
    }
    bool Wait(std::chrono::milliseconds delay)
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        return wake.wait_for(lock, delay, [&] { return Stopped(); });
    }
    void Publish(const std::vector<StreamQuote>& quotes)
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (!Stopped()) callback(generation, quotes);
    }
    void Status(const std::vector<std::size_t>& indexes, const std::wstring& error)
    {
        std::vector<StreamQuote> quotes;
        for (auto index : indexes) quotes.push_back({index, false, {}, error});
        Publish(quotes);
    }
    void Run(DataSource source, std::wstring category, std::vector<Instrument> instruments,
             std::vector<std::size_t> indexes)
    {
        int backoff = 1;
        StreamPlan plan;
        bool havePlan = false;
        while (!Stopped())
        {
            try
            {
                if (!havePlan)
                {
                    if (!IsSupportedMarket(source, category))
                        throw std::runtime_error("Unsupported exchange/market combination");
                    std::vector<std::string> metadata;
                    std::wstring error;
                    MarketClient client;
                    if (!client.FetchStreamMetadata(source, category, metadata, error))
                        throw std::runtime_error("Metadata: " + Utf8(error));
                    if (Stopped()) break;
                    plan = BuildStreamPlan(source, category, instruments, metadata);
                    havePlan = true;
                }
                Connect(plan, indexes, backoff);
            }
            catch (const std::exception& error)
            {
                Status(indexes, L"WebSocket 已断开，正在重连：" + Utf8ToWide(error.what()));
            }
            catch (...) { Status(indexes, L"WebSocket 异常，正在重连"); }
            if (Wait(std::chrono::seconds(backoff))) break;
            backoff = (std::min)(backoff * 2, 30);
        }
    }
    void Connect(const StreamPlan& plan, const std::vector<std::size_t>& indexes, int& backoff)
    {
        Status(indexes, L"WebSocket 连接中，等待订阅快照");
        Handle session(WinHttpOpen(L"TrafficMonitor-CryptoPrice-WebSocket/2.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.value) throw NetworkError("WinHttpOpen");
        WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 5000);
        Handle connection(WinHttpConnect(session.value, plan.host.c_str(), plan.port, 0));
        if (!connection.value) throw NetworkError("WinHttpConnect");
        Handle request(WinHttpOpenRequest(connection.value, L"GET", plan.path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
            throw NetworkError("WebSocket upgrade option");
        if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) throw NetworkError("WebSocket handshake");
        DWORD status{}, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 nullptr, &status, &size, nullptr) || status != 101)
            throw std::runtime_error("WebSocket HTTP " + std::to_string(status));
        Socket socket;
        socket.value = WinHttpWebSocketCompleteUpgrade(request.value, 0);
        if (!socket.value) throw NetworkError("WebSocket complete upgrade");
        const HINTERNET receiveSocket = socket.value;
        DWORD keepalive = 30000;
        WinHttpSetOption(receiveSocket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &keepalive, sizeof(keepalive));
        for (const auto& subscription : plan.subscriptions) socket.Send(subscription);
        std::map<std::string, Ticker> cache;
        // Transport traffic (including heartbeats) proves connection liveness;
        // absence of trades in a closed/illiquid market is not a stale socket.
        std::atomic<ULONGLONG> lastFrame{GetTickCount64()};
        std::atomic<bool> firstQuote{false};
        const auto connectedAt = GetTickCount64();
        std::atomic<bool> finished{false};
        std::mutex guardMutex;
        std::condition_variable guardWake;
        std::thread guard([&] {
            auto lastPing = GetTickCount64();
            std::unique_lock<std::mutex> lock(guardMutex);
            while (!finished.load())
            {
                guardWake.wait_for(lock, std::chrono::milliseconds(100));
                const auto now = GetTickCount64();
                if (Stopped() || now - lastFrame.load() > 75000 || (!firstQuote.load() && now - connectedAt > 30000))
                { socket.Close(); break; }
                if (now - lastPing >= 15000)
                {
                    try { socket.Send(plan.heartbeat); }
                    catch (...) { socket.Close(); break; }
                    lastPing = now;
                }
            }
        });
        const auto finish = [&] {
            finished.store(true); guardWake.notify_all(); socket.Close();
            if (guard.joinable()) guard.join();
        };
        std::set<std::string> seen;
        try
        {
            std::string message;
            char buffer[16384];
            while (!Stopped())
            {
                DWORD bytes{};
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                const DWORD result = WinHttpWebSocketReceive(receiveSocket, buffer, sizeof(buffer), &bytes, &type);
                if (result != NO_ERROR) throw std::runtime_error("WebSocket receive error " + std::to_string(result));
                if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) throw std::runtime_error("WebSocket peer closed");
                lastFrame.store(GetTickCount64());
                if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                    throw std::runtime_error("Unexpected binary WebSocket payload");
                message.append(buffer, bytes);
                if (message.size() > 2 * 1024 * 1024) throw std::runtime_error("WebSocket message too large");
                if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
                const auto changed = ApplyStreamMessage(plan, message, cache);
                message.clear();
                if (GetTickCount64() - connectedAt > 30000)
                {
                    std::vector<StreamQuote> missing;
                    for (auto index : indexes)
                        if (!seen.count(WideToAscii(instrumentsFor(index))))
                            missing.push_back({index, false, {}, L"已连接但尚未收到该标的快照，请检查订阅／市场状态"});
                    if (!missing.empty()) Publish(missing);
                }
                if (changed.empty()) continue;
                firstQuote.store(true);
                backoff = 1;
                std::vector<StreamQuote> updates;
                for (std::size_t i = 0; i < indexes.size(); ++i)
                {
                    const auto key = WideToAscii(instrumentsFor(indexes[i]));
                    if (std::find(changed.begin(), changed.end(), key) == changed.end()) continue;
                    const auto found = cache.find(key);
                    seen.insert(key);
                    if (found != cache.end()) updates.push_back({indexes[i], true, found->second, {}});
                }
                if (!updates.empty()) Publish(updates);
            }
        }
        catch (...) { finish(); throw; }
        finish();
    }
    // Full settings snapshot; indexes refer to this immutable vector.
    std::vector<Instrument> configured;
    const std::wstring& instrumentsFor(std::size_t index) const { return configured.at(index).symbol; }
};

StreamClient::~StreamClient() { Stop(); }
void StreamClient::Stop()
{
    if (current_) { current_->Cancel(); current_.reset(); }
}
void StreamClient::Start(const Settings& settings, unsigned long long generation, Callback callback)
{
    Stop();
    if (closing_->load() || settings.instruments.empty()) return;
    // TrafficMonitor has no pre-unload callback. Permanent streams cannot safely
    // execute after FreeLibrary. Keep the DLL mapped until process exit; no
    // network joins or static-singleton destruction occur under the loader lock.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&g_moduleInstance), &pinned))
        throw std::runtime_error("Cannot pin WebSocket plugin module");
    auto session = std::make_shared<Session>();
    session->closing = closing_; session->generation = generation;
    session->callback = std::move(callback); session->configured = settings.instruments;
    current_ = session;
    std::map<std::pair<DataSource, std::wstring>, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < settings.instruments.size(); ++i)
        groups[{InstrumentSource(settings.instruments[i]), settings.instruments[i].category}].push_back(i);
    try
    {
    for (const auto& [key, indexes] : groups)
    {
        std::vector<Instrument> instruments;
        for (auto index : indexes) instruments.push_back(settings.instruments[index]);
        std::thread([session, key, instruments = std::move(instruments), indexes] {
            session->Run(key.first, key.second, instruments, indexes);
        }).detach();
    }
    }
    catch (...) { session->Cancel(); current_.reset(); throw; }
}
} // namespace cryptoprice
