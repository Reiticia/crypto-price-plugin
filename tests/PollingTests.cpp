#include "StreamClient.h"
#include "Module.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <map>
#include <stdexcept>

HINSTANCE g_moduleInstance{};
int main()
{
    using namespace cryptoprice;
    using namespace std::chrono;
    g_moduleInstance = GetModuleHandleW(nullptr);
    int failures{};
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
    };
    std::mutex mutex;
    std::condition_variable wake;
    int calls{}, callbacks{}, active{}, maxActive{};
    bool block{}, release{}, throwRequest{}, complete{};
    unsigned long long latestGeneration{};
    std::vector<StreamQuote> latest;
    steady_clock::time_point first{}, second{};
    StreamClient client([&](DataSource source, const std::wstring& category, PriceType type,
                            std::unordered_map<std::string, Ticker>& tickers, std::wstring&) {
        std::unique_lock<std::mutex> lock(mutex);
        check(source == DataSource::Binance && category == L"SPOT" && type == PriceType::Mark,
              "poll uses configured exchange, category and price type");
        ++calls;
        ++active;
        maxActive = (std::max)(maxActive, active);
        if (calls == 1) first = steady_clock::now();
        if (calls == 2) second = steady_clock::now();
        wake.notify_all();
        wake.wait(lock, [&] { return !block || release; });
        --active;
        complete = true;
        wake.notify_all();
        if (throwRequest) throw std::runtime_error("simulated HTTP failure");
        Ticker ticker;
        ticker.lastPrice = std::to_string(calls);
        tickers["BTCUSDT"] = ticker;
        tickers["ETHUSDT"] = ticker;
        return true;
    });
    const auto callback = [&](unsigned long long generation, const std::vector<StreamQuote>& quotes) {
        std::lock_guard<std::mutex> lock(mutex);
        ++callbacks;
        latestGeneration = generation;
        latest = quotes;
        wake.notify_all();
    };
    const auto wait = [&](const auto& predicate) {
        std::unique_lock<std::mutex> lock(mutex);
        return wake.wait_for(lock, seconds(4), predicate);
    };
    Settings settings;
    settings.updateMode = UpdateMode::HttpPolling;
    settings.refreshSeconds = 2;
    settings.priceType = PriceType::Mark;
    settings.instruments = {{L"SPOT", L"BTCUSDT", 2, DataSource::Binance},
                            {L"SPOT", L"ETHUSDT", 2, DataSource::Binance},
                            {L"SPOT", L"MISSINGUSDT", 2, DataSource::Binance}};
    client.Start(settings, 1, callback);
    check(wait([&] { return callbacks >= 1; }), "HTTP polls immediately after start");
    {
        std::lock_guard<std::mutex> lock(mutex);
        check(calls == 1 && latest.size() == 3 && latest[0].success && latest[1].success && !latest[2].success,
              "one grouped HTTP request serves multiple symbols and reports missing symbol");
        check(latest[0].index == 0 && latest[1].index == 1 && latest[2].index == 2,
              "HTTP results retain configured indexes");
    }
    check(wait([&] { return callbacks >= 2; }), "HTTP repeats at configured interval");
    client.Stop();
    {
        std::lock_guard<std::mutex> lock(mutex);
        check(second - first >= milliseconds(1900) && maxActive == 1, "polls respect interval and never overlap within group");
        block = true;
        release = false;
        complete = false;
    }
    client.Start(settings, 2, callback);
    check(wait([&] { return active == 1; }), "slow HTTP request entered");
    const auto before = steady_clock::now();
    client.Stop();
    check(steady_clock::now() - before < milliseconds(500), "Stop does not wait for HTTP network completion");
    int stoppedCallbacks{};
    {
        std::lock_guard<std::mutex> lock(mutex);
        stoppedCallbacks = callbacks;
    }
    // Switch to WebSocket while an old HTTP request is still in flight. The
    // unsupported combination produces a deterministic error without a network.
    Settings websocket;
    websocket.instruments = {{L"USD-FUTURES", L"BTCUSD", 2, DataSource::Bitget}};
    client.Start(websocket, 3, callback);
    check(wait([&] { return latestGeneration == 3; }), "mode switch starts WebSocket path");
    client.Stop();
    {
        std::lock_guard<std::mutex> lock(mutex);
        check(calls == 3 && callbacks > stoppedCallbacks, "WebSocket path does not invoke HTTP polling transport");
        stoppedCallbacks = callbacks;
        release = true;
        wake.notify_all();
    }
    check(wait([&] { return complete && active == 0; }), "old HTTP request finishes privately");
    Sleep(150);
    {
        std::lock_guard<std::mutex> lock(mutex);
        check(callbacks == stoppedCallbacks, "cancelled HTTP result cannot publish after mode switch");
        block = false;
        throwRequest = true;
    }
    client.Start(settings, 4, callback);
    check(wait([&] { return latestGeneration == 4; }), "HTTP can restart after switching back");
    client.Stop();
    {
        std::lock_guard<std::mutex> lock(mutex);
        check(latest.size() == 3 && !latest[0].success && !latest[1].success && !latest[2].success,
              "HTTP exceptions become per-instrument errors");
    }
    // Stop must interrupt the polling wait rather than leave another request scheduled.
    int stoppedCalls{};
    { std::lock_guard<std::mutex> lock(mutex); stoppedCalls = calls; }
    Sleep(2100);
    { std::lock_guard<std::mutex> lock(mutex); check(calls == stoppedCalls, "Stop cancels scheduled polling"); }
    client.Shutdown();
    client.Start(settings, 5, callback);
    Settings empty;
    empty.updateMode = UpdateMode::HttpPolling;
    empty.instruments.clear();
    StreamClient emptyClient;
    emptyClient.Start(empty, 6, callback);
    Sleep(100);
    { std::lock_guard<std::mutex> lock(mutex); check(calls == stoppedCalls, "shutdown prevents new HTTP requests"); }
    // Different market/source groups must stay separate, retain global indexes,
    // and allow healthy groups to publish even when another request fails.
    std::map<std::pair<DataSource, std::wstring>, int> groupCalls;
    bool recovered{};
    StreamClient grouped([&](DataSource source, const std::wstring& category, PriceType,
                             std::unordered_map<std::string, Ticker>& tickers, std::wstring& error) {
        std::lock_guard<std::mutex> lock(mutex);
        const int attempt = ++groupCalls[{source, category}];
        if (source == DataSource::Okx && attempt == 1) { error = L"HTTP 503"; return false; }
        Ticker ticker;
        ticker.lastPrice = "123";
        tickers["BTCUSDT"] = ticker;
        tickers["ETHUSDT"] = ticker;
        return true;
    });
    settings.instruments = {{L"SPOT", L"BTCUSDT", 2, DataSource::Binance},
                            {L"SPOT", L"ETHUSDT", 2, DataSource::Okx},
                            {L"USDT-FUTURES", L"BTCUSDT", 2, DataSource::Binance}};
    grouped.Start(settings, 7, [&](unsigned long long generation, const std::vector<StreamQuote>& quotes) {
        std::lock_guard<std::mutex> lock(mutex);
        check(generation == 7 && quotes.size() == 1 && quotes[0].index < 3,
              "separate source/market groups retain global indexes");
        if (!quotes.empty() && quotes[0].index == 1)
        {
            if (groupCalls[{DataSource::Okx, L"SPOT"}] == 1)
                check(!quotes[0].success && quotes[0].error == L"HTTP 503", "HTTP failure publishes exchange error");
            else recovered = quotes[0].success;
        }
        wake.notify_all();
    });
    check(wait([&] { return recovered; }), "HTTP polling recovers on next scheduled request");
    grouped.Stop();
    {
        std::lock_guard<std::mutex> lock(mutex);
        check(groupCalls.size() == 3, "poll requests are grouped by exchange and market");
    }
    return failures ? 1 : 0;
}
