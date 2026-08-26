#include "MarketClient.h"

#include <Windows.h>

#include <iostream>
#include <string>
#include <unordered_map>

namespace
{
std::string ToUtf8(const std::wstring& value)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        utf8.data(), size, nullptr, nullptr);
    return utf8;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    const std::wstring sourceName = argc > 1 ? argv[1] : L"okx";
    cryptoprice::DataSource source = sourceName == L"bitget" ? cryptoprice::DataSource::Bitget :
                                sourceName == L"binance" ? cryptoprice::DataSource::Binance :
                                cryptoprice::DataSource::Okx;
    cryptoprice::MarketClient client;
    std::unordered_map<std::string, cryptoprice::Ticker> tickers;
    std::wstring error;
    const bool ok = client.FetchCategory(source, L"USDT-FUTURES",
                                         cryptoprice::PriceType::Mark, tickers, error);
    std::cout << "source=" << ToUtf8(sourceName) << " ok=" << ok
              << " count=" << tickers.size() << " error=" << ToUtf8(error) << '\n';
    return ok ? 0 : 1;
}
