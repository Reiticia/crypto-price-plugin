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
    Check(cryptoprice::NormalizeSymbol(L" btc/usdt ") == L"BTCUSDT", "normalize slash");
    Check(cryptoprice::NormalizeSymbol(L"eth-usdc") == L"ETHUSDC", "normalize hyphen");
    Check(cryptoprice::IsValidSymbol(L"BTCUSDT"), "valid symbol");
    Check(!cryptoprice::IsValidSymbol(L"BTC$USDT"), "invalid symbol");
    Check(cryptoprice::FormatInstrumentSymbol({L"USDT-FUTURES", L"BTCUSDT"}) == L"BTCUSDT",
          "futures display symbol");
    Check(cryptoprice::FormatInstrumentSymbol({L"SPOT", L"BTCUSDT"}) == L"BTC/USDT",
          "spot display symbol");
    Check(cryptoprice::DataSourceFromKey(L"okx") == cryptoprice::DataSource::Okx, "parse OKX source");
    Check(cryptoprice::DataSourceFromKey(L"binance") == cryptoprice::DataSource::Binance,
          "parse Binance source");
    Check(std::wstring(cryptoprice::DataSourceKey(cryptoprice::DataSource::Bitget)) == L"bitget",
          "serialize Bitget source");

    const auto dohAddresses = cryptoprice::ParseDohIpv4Addresses(
        R"({"Answer":[{"data":"eea.okx.com.cdn.cloudflare.net."},{"data":"104.18.43.174"},{"data":"172.64.144.82"},{"data":"104.18.43.174"}]})");
    Check(dohAddresses.size() == 2 && dohAddresses[0] == "104.18.43.174" &&
          dohAddresses[1] == "172.64.144.82", "parse unique DoH IPv4 addresses");

    const auto googleDohAddresses = cryptoprice::ParseDohIpv4Addresses(
        R"({"Status":0,"Answer":[{"name":"www.okx.com.","type":5,"TTL":300,"data":"www.okx.com.cdn.cloudflare.net."},{"name":"www.okx.com.cdn.cloudflare.net.","type":1,"TTL":300,"data":"172.64.144.82"},{"name":"www.okx.com.cdn.cloudflare.net.","type":1,"TTL":300,"data":"104.18.43.174"}]})");
    Check(googleDohAddresses.size() == 2 &&
          googleDohAddresses[0] == "172.64.144.82" &&
          googleDohAddresses[1] == "104.18.43.174", "parse google DoH IPv4 addresses");

    const std::string response = R"({
        "code":"00000","msg":"success","data":[
          {"category":"USDT-FUTURES","symbol":"BTCUSDT","lastPrice":"90216.3",
           "price24hPcnt":"-0.02605","indexPrice":"90247.54","markPrice":"90216.4","ts":"1765"},
          {"category":"SPOT","symbol":"ETHUSDT","lastPrice":"3100.25",
           "price24hPcnt":"0.012","ts":"1766"}
        ]
    })";
    std::unordered_map<std::string, cryptoprice::Ticker> tickers;
    std::string error;
    Check(cryptoprice::ParseTickerResponse(response, tickers, error), "parse successful response");
    Check(tickers.size() == 2, "parse all tickers");
    Check(tickers["BTCUSDT"].markPrice == "90216.4", "parse mark price");
    Check(tickers["ETHUSDT"].price24hPcnt == "0.012", "parse price change");
    Check(tickers["ETHUSDT"].markPrice.empty(), "spot response has no mark price");

    Check(!cryptoprice::ParseTickerResponse(R"({"code":"40034","msg":"Parameter error","data":[]})",
                                       tickers, error), "reject API error");
    Check(error.find("40034") != std::string::npos, "report API error code");

    const std::string okxResponse = R"({"code":"0","msg":"","data":[
      {"instId":"BTC-USDT-SWAP","last":"80000.1","open24h":"79000","ts":"1"}
    ]})";
    Check(cryptoprice::ParseOkxTickerResponse(okxResponse, tickers, error), "parse OKX ticker");
    Check(tickers.count("BTCUSDT") == 1, "normalize OKX swap symbol");
    Check(std::abs(std::stod(tickers["BTCUSDT"].price24hPcnt) -
                   ((80000.1 - 79000.0) / 79000.0)) < 0.000001, "calculate OKX change");
    Check(cryptoprice::OverlayOkxPrices(
        R"({"code":"0","data":[{"instId":"BTC-USDT-SWAP","markPx":"79999.9"}]})",
        "markPx", true, tickers, error), "overlay OKX mark price");
    Check(tickers["BTCUSDT"].markPrice == "79999.9", "read OKX mark price");

    Check(cryptoprice::ParseBinanceTickerResponse(
        R"([{"symbol":"ETHUSDT","lastPrice":"3100.25","priceChangePercent":"2.5"}])",
        false, tickers, error), "parse Binance ticker");
    Check(std::abs(std::stod(tickers["ETHUSDT"].price24hPcnt) - 0.025) < 0.000001,
          "convert Binance percentage to fraction");
    Check(cryptoprice::OverlayBinancePremium(
        R"([{"symbol":"ETHUSDT","markPrice":"3100.1","indexPrice":"3100.2"}])",
        false, tickers, error), "overlay Binance premium index");
    Check(tickers["ETHUSDT"].indexPrice == "3100.2", "read Binance index price");

    Check(cryptoprice::ParseBinanceTickerResponse(
        R"([{"symbol":"BTCUSD_PERP","lastPrice":"80001","priceChangePercent":"1.2"},
             {"symbol":"BTCUSD_260925","lastPrice":"80002","priceChangePercent":"1.3"}])",
        true, tickers, error), "parse Binance coin futures");
    Check(tickers.size() == 1 && tickers.count("BTCUSD") == 1,
          "keep Binance coin perpetual and normalize symbol");

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
