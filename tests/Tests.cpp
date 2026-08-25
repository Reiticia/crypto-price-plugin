#include "Config.h"
#include "Json.h"

#include <iostream>
#include <cmath>
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
    Check(bitget::FormatInstrumentSymbol({L"USDT-FUTURES", L"BTCUSDT"}) == L"BTCUSDT",
          "futures display symbol");
    Check(bitget::FormatInstrumentSymbol({L"SPOT", L"BTCUSDT"}) == L"BTC/USDT",
          "spot display symbol");
    Check(bitget::DataSourceFromKey(L"okx") == bitget::DataSource::Okx, "parse OKX source");
    Check(bitget::DataSourceFromKey(L"binance") == bitget::DataSource::Binance,
          "parse Binance source");
    Check(std::wstring(bitget::DataSourceKey(bitget::DataSource::Bitget)) == L"bitget",
          "serialize Bitget source");

    const std::string response = R"({
        "code":"00000","msg":"success","data":[
          {"category":"USDT-FUTURES","symbol":"BTCUSDT","lastPrice":"90216.3",
           "price24hPcnt":"-0.02605","indexPrice":"90247.54","markPrice":"90216.4","ts":"1765"},
          {"category":"SPOT","symbol":"ETHUSDT","lastPrice":"3100.25",
           "price24hPcnt":"0.012","ts":"1766"}
        ]
    })";
    std::unordered_map<std::string, bitget::Ticker> tickers;
    std::string error;
    Check(bitget::ParseTickerResponse(response, tickers, error), "parse successful response");
    Check(tickers.size() == 2, "parse all tickers");
    Check(tickers["BTCUSDT"].markPrice == "90216.4", "parse mark price");
    Check(tickers["ETHUSDT"].price24hPcnt == "0.012", "parse price change");
    Check(tickers["ETHUSDT"].markPrice.empty(), "spot response has no mark price");

    Check(!bitget::ParseTickerResponse(R"({"code":"40034","msg":"Parameter error","data":[]})",
                                       tickers, error), "reject API error");
    Check(error.find("40034") != std::string::npos, "report API error code");

    const std::string okxResponse = R"({"code":"0","msg":"","data":[
      {"instId":"BTC-USDT-SWAP","last":"80000.1","open24h":"79000","ts":"1"}
    ]})";
    Check(bitget::ParseOkxTickerResponse(okxResponse, tickers, error), "parse OKX ticker");
    Check(tickers.count("BTCUSDT") == 1, "normalize OKX swap symbol");
    Check(std::abs(std::stod(tickers["BTCUSDT"].price24hPcnt) -
                   ((80000.1 - 79000.0) / 79000.0)) < 0.000001, "calculate OKX change");
    Check(bitget::OverlayOkxPrices(
        R"({"code":"0","data":[{"instId":"BTC-USDT-SWAP","markPx":"79999.9"}]})",
        "markPx", true, tickers, error), "overlay OKX mark price");
    Check(tickers["BTCUSDT"].markPrice == "79999.9", "read OKX mark price");

    Check(bitget::ParseBinanceTickerResponse(
        R"([{"symbol":"ETHUSDT","lastPrice":"3100.25","priceChangePercent":"2.5"}])",
        false, tickers, error), "parse Binance ticker");
    Check(std::abs(std::stod(tickers["ETHUSDT"].price24hPcnt) - 0.025) < 0.000001,
          "convert Binance percentage to fraction");
    Check(bitget::OverlayBinancePremium(
        R"([{"symbol":"ETHUSDT","markPrice":"3100.1","indexPrice":"3100.2"}])",
        false, tickers, error), "overlay Binance premium index");
    Check(tickers["ETHUSDT"].indexPrice == "3100.2", "read Binance index price");

    Check(bitget::ParseBinanceTickerResponse(
        R"([{"symbol":"BTCUSD_PERP","lastPrice":"80001","priceChangePercent":"1.2"},
             {"symbol":"BTCUSD_260925","lastPrice":"80002","priceChangePercent":"1.3"}])",
        true, tickers, error), "parse Binance coin futures");
    Check(tickers.size() == 1 && tickers.count("BTCUSD") == 1,
          "keep Binance coin perpetual and normalize symbol");

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
