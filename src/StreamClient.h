#pragma once

#include "Config.h"
#include "Json.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace cryptoprice
{
extern std::atomic<bool> g_processStopping;
struct StreamQuote
{
    std::size_t index{};
    bool success{};
    Ticker ticker;
    std::wstring error;
};

class StreamClient
{
public:
    using Callback = std::function<void(unsigned long long, const std::vector<StreamQuote>&)>;
    using Fetcher = std::function<bool(DataSource, const std::wstring&, PriceType,
        std::unordered_map<std::string, Ticker>&, std::wstring&)>;
    // Optional transport injection for deterministic HTTP polling tests.
    explicit StreamClient(Fetcher fetcher = {});
    ~StreamClient();
    void Start(const Settings& settings, unsigned long long generation, Callback callback);
    // Signals only; workers own their session state and a DLL reference. Never
    // join network threads from DllMain or the host UI thread. Start selects
    // WebSocket or grouped HTTP polling using settings.updateMode.
    void Stop();
    void Shutdown() noexcept { closing_->store(true); }
private:
    struct Session;
    Fetcher fetcher_;
    std::shared_ptr<Session> current_;
    std::shared_ptr<std::atomic<bool>> closing_{std::make_shared<std::atomic<bool>>(false)};
};
} // namespace cryptoprice
