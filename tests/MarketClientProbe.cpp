#include "MarketClient.h"

#include <Windows.h>

#include <chrono>
#include <iostream>
#include <unordered_map>

int wmain()
{
    cryptoprice::MarketClient client;
    std::unordered_map<std::string, cryptoprice::Ticker> tickers;
    std::wstring error;
    const auto started = std::chrono::steady_clock::now();
    const bool ok = client.FetchCategory(cryptoprice::DataSource::Okx, L"USDT-FUTURES",
                                         cryptoprice::PriceType::Mark, tickers, error);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, error.data(),
        static_cast<int>(error.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(utf8Size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, error.data(), static_cast<int>(error.size()),
        utf8.data(), utf8Size, nullptr, nullptr);
    std::cout << "ok=" << ok << " count=" << tickers.size() << " elapsed=" << elapsed
              << "ms error=" << utf8 << '\n';
    return ok ? 0 : 1;
}
