#include "StreamClient.h"
#include "StreamProtocol.h"
#include "StreamSnapshots.h"
#include "StreamSocket.h"
#include "MarketClient.h"
#include "Module.h"

#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <stdexcept>
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
    void Poll(DataSource source, const std::wstring& category, const std::vector<Instrument>& instruments,
              const std::vector<std::size_t>& indexes, PriceType priceType, int seconds, const Fetcher& fetch)
    {
        // One sequential request per exchange/market group: never overlap slow
        // polls or accumulate missed ticks. Cancellation interrupts the wait;
        // an in-flight HTTP call finishes privately and cannot publish after Stop.
        while (!Stopped())
        {
            const auto started = std::chrono::steady_clock::now();
            std::vector<StreamQuote> updates;
            try
            {
                std::unordered_map<std::string, Ticker> tickers;
                std::wstring error;
                const bool success = fetch(source, category, priceType, tickers, error);
                for (std::size_t i = 0; i < indexes.size(); ++i)
                {
                    const auto found = tickers.find(WideToAscii(instruments[i].symbol));
                    if (!success) updates.push_back({indexes[i], false, {}, error.empty() ? L"HTTP 行情获取失败" : error});
                    else if (found == tickers.end())
                        updates.push_back({indexes[i], false, {}, L"未找到该标的，请检查市场类型和交易对"});
                    else updates.push_back({indexes[i], true, found->second, {}});
                }
            }
            catch (const std::exception& error)
            {
                for (auto index : indexes) updates.push_back({index, false, {}, Utf8ToWide(error.what())});
            }
            catch (...)
            {
                for (auto index : indexes) updates.push_back({index, false, {}, L"HTTP 行情请求异常"});
            }
            Publish(updates);
            const auto elapsed = std::chrono::steady_clock::now() - started;
            // If a request outlasts the interval, wait a full interval before
            // retrying rather than issuing catch-up requests in a tight loop.
            const auto interval = std::chrono::seconds(seconds);
            const auto delay = elapsed < interval ? interval - elapsed : interval;
            if (Wait(std::chrono::duration_cast<std::chrono::milliseconds>(delay))) break;
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
        StreamSocket socket(WinHttpWebSocketCompleteUpgrade(request.value, 0));
        if (!socket.Get()) throw NetworkError("WebSocket complete upgrade");
        DWORD keepalive = 30000;
        WinHttpSetOption(socket.Get(), WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &keepalive, sizeof(keepalive));
        DWORD closeTimeout = 1000;
        if (!WinHttpSetOption(socket.Get(), WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &closeTimeout, sizeof(closeTimeout)))
            throw NetworkError("WebSocket close timeout");
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
                { socket.Interrupt(); break; }
                if (now - lastPing >= 15000)
                {
                    try { socket.Send(plan.heartbeat); }
                    catch (...) { socket.Interrupt(); break; }
                    lastPing = now;
                }
            }
        });
        const auto finish = [&] {
            finished.store(true); guardWake.notify_all();
            if (guard.joinable()) guard.join();
            // Receive has returned and the guard can no longer send/close.
            // Only the socket destructor may now release the handle.
        };
        StreamSnapshots snapshots;
        try
        {
            std::string message;
            char buffer[16384];
            while (!Stopped())
            {
                DWORD bytes{};
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                const DWORD result = socket.Receive(buffer, sizeof(buffer), bytes, type);
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
                if (!changed.empty())
                {
                    firstQuote.store(true);
                    backoff = 1;
                }
                std::vector<StreamQuote> updates;
                for (std::size_t i = 0; i < indexes.size(); ++i)
                {
                    const auto key = WideToAscii(instrumentsFor(indexes[i]));
                    if (std::find(changed.begin(), changed.end(), key) == changed.end()) continue;
                    const auto found = cache.find(key);
                    if (found != cache.end())
                    {
                        snapshots.Observe(indexes[i]);
                        updates.push_back({indexes[i], true, found->second, {}});
                    }
                }
                if (!updates.empty()) Publish(updates);
                // Process this frame's snapshots first, then warn once per
                // missing instrument (including on heartbeat-only frames).
                std::vector<StreamQuote> missing;
                for (auto index : snapshots.Missing(indexes, std::chrono::milliseconds(GetTickCount64() - connectedAt)))
                    missing.push_back({index, false, {}, L"已连接但尚未收到该标的快照，请检查订阅／市场状态"});
                if (!missing.empty()) Publish(missing);
            }
        }
        catch (...) { finish(); throw; }
        finish();
    }
    // Full settings snapshot; indexes refer to this immutable vector.
    std::vector<Instrument> configured;
    const std::wstring& instrumentsFor(std::size_t index) const { return configured.at(index).symbol; }
};

StreamClient::StreamClient(Fetcher fetcher) : fetcher_(std::move(fetcher))
{
    if (!fetcher_) fetcher_ = [](DataSource source, const std::wstring& category, PriceType type,
                               std::unordered_map<std::string, Ticker>& tickers, std::wstring& error) {
        return MarketClient{}.FetchCategory(source, category, type, tickers, error);
    };
}
StreamClient::~StreamClient() { Stop(); }
void StreamClient::Stop()
{
    if (current_) { current_->Cancel(); current_.reset(); }
}
void StreamClient::Start(const Settings& settings, unsigned long long generation, Callback callback)
{
    Stop();
    if (closing_->load() || settings.instruments.empty()) return;
    // TrafficMonitor has no pre-unload callback. Background requests cannot safely
    // execute after FreeLibrary. Keep the DLL mapped until process exit; no
    // network joins or static-singleton destruction occur under the loader lock.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&g_moduleInstance), &pinned))
        throw std::runtime_error("Cannot pin market data plugin module");
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
        std::thread([session, key, instruments = std::move(instruments), indexes,
                     mode = settings.updateMode, type = settings.priceType,
                     seconds = std::clamp(settings.refreshSeconds, 2, 300), fetch = fetcher_] {
            if (mode == UpdateMode::HttpPolling)
                session->Poll(key.first, key.second, instruments, indexes, type, seconds, fetch);
            else session->Run(key.first, key.second, instruments, indexes);
        }).detach();
    }
    }
    catch (...) { session->Cancel(); current_.reset(); throw; }
}
} // namespace cryptoprice
