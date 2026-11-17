#include "MarketClient.h"
#include <iostream>

// Explicit opt-in network test; not registered in CTest.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) { std::cerr << "Usage: ExchangeLiveSmoke <kraken|bybit> <category> <symbol> <last|mark|index>\n"; return 2; }
    using namespace cryptoprice;
    MarketClient client;
    std::unordered_map<std::string, Ticker> quotes;
    std::wstring error;
    const auto source = DataSourceFromKey(argv[1]);
    const auto type = PriceTypeFromKey(argv[4]);
    if (!client.FetchCategory(source, argv[2], type, quotes, error))
    { std::wcerr << error << '\n'; return 1; }
    const auto found = quotes.find(WideToAscii(argv[3]));
    if (found == quotes.end()) { std::cerr << "Symbol not returned\n"; return 3; }
    const auto& q = found->second;
    const std::string& price = std::wstring(argv[2]) == L"SPOT" || type == PriceType::Last ? q.lastPrice :
                               type == PriceType::Mark ? q.markPrice : q.indexPrice;
    if (price.empty()) return 4;
    std::cout << "tickers=" << quotes.size() << " symbol=" << q.symbol << " last=" << q.lastPrice
              << " mark=" << q.markPrice << " index=" << q.indexPrice << " change=" << q.price24hPcnt << '\n';
    return 0;
}
