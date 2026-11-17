#include "StreamClient.h"
#include "Module.h"
#include <Windows.h>
#include <condition_variable>
#include <iostream>
#include <mutex>

HINSTANCE g_moduleInstance{};
int main()
{
    using namespace cryptoprice;
    g_moduleInstance = GetModuleHandleW(nullptr);
    Settings settings;
    // Invalid combination fails before any metadata or network request.
    settings.instruments = {{L"USD-FUTURES", L"BTCUSD", 2, DataSource::Bitget}};
    std::mutex mutex;
    std::condition_variable wake;
    int callbacks{};
    unsigned long long latest{};
    const auto callback = [&](unsigned long long generation, const std::vector<StreamQuote>& quotes) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!quotes.empty() && !quotes.front().success) { ++callbacks; latest = generation; }
        wake.notify_all();
    };
    StreamClient stream;
    stream.Start(settings, 1, callback);
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!wake.wait_for(lock, std::chrono::seconds(2), [&] { return callbacks > 0; })) return 1;
    }
    const auto beforeStop = std::chrono::steady_clock::now();
    stream.Stop();
    if (std::chrono::steady_clock::now() - beforeStop > std::chrono::milliseconds(500)) return 2;
    int stoppedCount{};
    { std::lock_guard<std::mutex> lock(mutex); stoppedCount = callbacks; }
    Sleep(1200); // Longer than first retry; no callbacks may follow Stop.
    { std::lock_guard<std::mutex> lock(mutex); if (callbacks != stoppedCount) return 3; }
    stream.Start(settings, 2, callback);
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!wake.wait_for(lock, std::chrono::seconds(2), [&] { return latest == 2; })) return 4;
    }
    stream.Stop();
    // Destroy a session immediately after launching: workers own a valid stop token.
    { StreamClient shortLived; shortLived.Start(settings, 3, callback); }
    Sleep(150);
    stream.Shutdown();
    int shutdownCount{};
    { std::lock_guard<std::mutex> lock(mutex); shutdownCount = callbacks; }
    stream.Start(settings, 4, callback);
    Sleep(150);
    { std::lock_guard<std::mutex> lock(mutex); if (callbacks != shutdownCount) return 5; }
    std::cout << "Stream lifecycle tests passed\n";
    return 0;
}
