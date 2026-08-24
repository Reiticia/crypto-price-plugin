#include "Config.h"
#include "Json.h"

#include <iostream>
#include <string>
#include <unordered_map>

namespace
{
int failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    Check(bitget::NormalizeSymbol(L" btc/usdt ") == L"BTCUSDT", "normalize slash");
    Check(bitget::NormalizeSymbol(L"eth-usdc") == L"ETHUSDC", "normalize hyphen");
    Check(bitget::IsValidSymbol(L"BTCUSDT"), "valid symbol");
    Check(!bitget::IsValidSymbol(L"BTC$USDT"), "invalid symbol");

    const std::string response = R"({
        "code":"00000","msg":"success","data":[
          {"category":"USDT-FUTURES","symbol":"BTCUSDT","lastPrice":"90216.3",
           "price24hPcnt":"-0.02605","indexPrice":"90247.54","markPrice":"90216.4","ts":"1765"},
          {"category":"USDT-FUTURES","symbol":"ETHUSDT","lastPrice":"3100.25",
           "price24hPcnt":"0.012","indexPrice":"3101","markPrice":"3100.5","ts":"1766"}
        ]
    })";
    std::unordered_map<std::string, bitget::Ticker> tickers;
    std::string error;
    Check(bitget::ParseTickerResponse(response, tickers, error), "parse successful response");
    Check(tickers.size() == 2, "parse all tickers");
    Check(tickers["BTCUSDT"].markPrice == "90216.4", "parse mark price");
    Check(tickers["ETHUSDT"].price24hPcnt == "0.012", "parse price change");

    Check(!bitget::ParseTickerResponse(R"({"code":"40034","msg":"Parameter error","data":[]})",
                                       tickers, error), "reject API error");
    Check(error.find("40034") != std::string::npos, "report API error code");

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
