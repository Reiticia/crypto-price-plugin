#include "StreamClient.h"
#include "MarketClient.h"
#include <Windows.h>
#include <condition_variable>
#include <iostream>
#include <mutex>

HINSTANCE g_moduleInstance{};
int wmain(int argc, wchar_t** argv)
{
    if (argc != 4 && argc != 5) return 2;
    const bool hold = argc == 5 && std::wstring(argv[4]) == L"--hold";
    const bool reconnect = argc == 5 && std::wstring(argv[4]) == L"--reconnect";
    using namespace cryptoprice;
    g_moduleInstance = GetModuleHandleW(nullptr);
    Settings settings;
    settings.instruments = {{argv[2], argv[3], 2, DataSourceFromKey(argv[1])}};
    StreamClient stream;
    std::mutex mutex;
    std::condition_variable wake;
    int updates{};
    int newGenerationUpdates{};
    bool acceptOldGeneration{true};
    bool oldCallbackAfterStop{};
    bool lateUpdate{};
    const auto started = std::chrono::steady_clock::now();
    const StreamClient::Callback callback = [&](unsigned long long generation, const std::vector<StreamQuote>& quotes) {
        std::lock_guard<std::mutex> lock(mutex);
        if (generation == 1 && !acceptOldGeneration) oldCallbackAfterStop = true;
        for (const auto& q : quotes)
        {
            if (q.success && !q.ticker.lastPrice.empty())
            {
                ++updates;
                if (generation == 2) ++newGenerationUpdates;
                if (std::chrono::steady_clock::now() - started > std::chrono::seconds(18)) lateUpdate = true;
                if (updates <= 2 || updates % 100 == 0) std::cout << "stream=" << WideToAscii(argv[1]) << " symbol=" << q.ticker.symbol << " last=" << q.ticker.lastPrice
                          << " mark=" << q.ticker.markPrice << " index=" << q.ticker.indexPrice << '\n' << std::flush;
            }
            else
            {
                const int length = WideCharToMultiByte(CP_UTF8, 0, q.error.data(), static_cast<int>(q.error.size()), nullptr, 0, nullptr, nullptr);
                std::string text(static_cast<std::size_t>(length), '\0');
                WideCharToMultiByte(CP_UTF8, 0, q.error.data(), static_cast<int>(q.error.size()), text.data(), length, nullptr, nullptr);
                std::cout << text << '\n' << std::flush;
            }
        }
        wake.notify_all();
    };
    stream.Start(settings, 1, callback);
    {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::seconds(35), [&] { return hold ? lateUpdate : updates >= 2; });
    }
    stream.Stop();
    if (reconnect && updates)
    {
        { std::lock_guard<std::mutex> lock(mutex); acceptOldGeneration = false; }
        stream.Start(settings, 2, callback);
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::seconds(35), [&] { return newGenerationUpdates > 0; });
        }
        stream.Stop();
    }
    Sleep(250);
    if (reconnect) return newGenerationUpdates > 0 && !oldCallbackAfterStop ? 0 : 1;
    return (hold ? lateUpdate : updates > 0) ? 0 : 1;
}
