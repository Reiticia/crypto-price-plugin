#pragma once

#include <Windows.h>
#include <winhttp.h>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cryptoprice
{
// Owns one upgraded handle. The owner must join all users before destruction.
class StreamSocket
{
public:
    explicit StreamSocket(HINTERNET value) : value_(value) {}
    ~StreamSocket() { if (value_) WinHttpCloseHandle(value_); }
    StreamSocket(const StreamSocket&) = delete;
    StreamSocket& operator=(const StreamSocket&) = delete;

    HINTERNET Get() const { return value_; }
    void Send(const std::string& text)
    {
        if (text.empty()) return;
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (interrupted_.load()) throw std::runtime_error("WebSocket closed");
        const DWORD error = WinHttpWebSocketSend(value_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            const_cast<char*>(text.data()), static_cast<DWORD>(text.size()));
        if (error != NO_ERROR) throw std::runtime_error("WebSocket send error " + std::to_string(error));
    }
    DWORD Receive(void* buffer, DWORD size, DWORD& bytes, WINHTTP_WEB_SOCKET_BUFFER_TYPE& type)
    {
        if (interrupted_.load()) return ERROR_WINHTTP_OPERATION_CANCELLED;
        return WinHttpWebSocketReceive(value_, buffer, size, &bytes, &type);
    }
    DWORD Interrupt()
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (interrupted_.exchange(true)) return NO_ERROR;
        // Unlike CloseHandle, WebSocketClose explicitly cancels a pending
        // receive. Serialize it with sends, but keep the handle alive until
        // both this call and the receiver have returned.
        return WinHttpWebSocketClose(value_, WINHTTP_WEB_SOCKET_ENDPOINT_TERMINATED_CLOSE_STATUS, nullptr, 0);
    }

private:
    const HINTERNET value_;
    std::mutex sendMutex_;
    std::atomic<bool> interrupted_{false};
};
} // namespace cryptoprice
