#include "Config.h"
#include "Json.h"
#include "InstrumentValidation.h"
#include "Tooltip.h"

#include <Windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
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

void TestInstrumentValidation()
{
    using namespace cryptoprice;
    Settings settings;
    settings.instruments = {{L"USDT-FUTURES", L"BTCUSDT"}, {L"USDT-FUTURES", L"ETHUSDT"}};
    std::unordered_map<std::string, Ticker> quotes;
    quotes["BTCUSDT"].lastPrice = "80000.12";
    quotes["BTCUSDT"].markPrice = "80001";
    quotes["BTCUSDT"].indexPrice = "80002";
    quotes["ETHUSDT"].lastPrice = "3000";
    quotes["ETHUSDT"].markPrice = "3001";
    quotes["ETHUSDT"].indexPrice = "3002";
    int calls = 0;
    bool networkSuccess = true;
    CategoryFetcher fetch = [&](DataSource source, const std::wstring& category, PriceType type,
                                std::unordered_map<std::string, Ticker>& tickers, std::wstring& error) {
        ++calls;
        Check(source == InstrumentSource(settings.instruments.front()), "validate per-instrument data source");
        Check(type == settings.priceType, "validate selected price type");
        Check(!category.empty(), "validate selected category");
        tickers = quotes;
        error = networkSuccess ? L"" : L"network unavailable";
        return networkSuccess;
    };
    for (DataSource source : {DataSource::Bitget, DataSource::Okx, DataSource::Binance})
    {
        for (auto& instrument : settings.instruments) instrument.dataSource = source;
        for (PriceType type : {PriceType::Last, PriceType::Mark, PriceType::Index})
        {
            settings.priceType = type;
            calls = 0;
            Check(ValidateInstruments(settings, fetch).empty(), "accept queryable price for each source/type");
            Check(calls == 1, "fetch each category once per validation");
        }
    }
    settings.priceType = PriceType::Last;
    settings.instruments[1].symbol = L"NOTEXISTUSDT";
    Check(ValidateInstruments(settings, fetch).find(L"NOTEXISTUSDT") != std::wstring::npos,
          "reject missing symbol and identify it in error");
    settings.instruments.resize(1);
    networkSuccess = false;
    Check(ValidateInstruments(settings, fetch).find(L"network unavailable") != std::wstring::npos,
          "reject query failure, even when callback returned tickers");
    networkSuccess = true;
    for (const std::string raw : {"", "abc", "12oops", "nan", "inf", "1e999", "0", "-1"})
    {
        quotes["BTCUSDT"].lastPrice = raw;
        Check(!ValidateInstruments(settings, fetch).empty(), "reject missing, malformed or nonpositive price");
    }
    quotes["BTCUSDT"].lastPrice = std::string("12\0oops", 7);
    Check(!ValidateInstruments(settings, fetch).empty(), "reject embedded NUL in price");
    quotes["BTCUSDT"].lastPrice = "0.00000001";
    Check(ValidateInstruments(settings, fetch).empty(), "accept low positive price");
    settings.priceType = PriceType::Mark;
    quotes["BTCUSDT"].markPrice.clear();
    Check(!ValidateInstruments(settings, fetch).empty(), "do not fall back to last when mark is missing");
    settings.priceType = PriceType::Index;
    quotes["BTCUSDT"].indexPrice.clear();
    Check(!ValidateInstruments(settings, fetch).empty(), "do not fall back to last when index is missing");
    settings.instruments[0].category = L"SPOT";
    Check(ValidateInstruments(settings, fetch).empty(), "spot uses last even when index selected");
    settings.priceType = PriceType::Mark;
    Check(ValidateInstruments(settings, fetch).empty(), "spot uses last even when mark selected");
    settings.instruments[0].category = L"USDC-FUTURES";
    Check(!ValidateInstruments(settings, fetch).empty(), "reject wrong futures category despite broad API response");
    settings.instruments[0] = {L"COIN-FUTURES", L"BTCUSD"};
    quotes["BTCUSD"].markPrice = "80000";
    Check(ValidateInstruments(settings, fetch).empty(), "accept coin margined price");
    settings.instruments[0] = {L"USDC-FUTURES", L"BTCUSDC"};
    quotes["BTCUSDC"].markPrice = "80000";
    settings.instruments.push_back({L"SPOT", L"BTCUSDT"});
    calls = 0;
    Check(ValidateInstruments(settings, fetch).empty() && calls == 2, "validate mixed categories separately");
    settings.instruments[0].symbol = L"BAD$SYMBOL";
    Check(!ValidateInstruments(settings, fetch).empty(), "reject invalid symbol without trusting quotes");
    settings.instruments = {{L"US-STOCK", L"F"}, {L"US-STOCK", L"TSLA"}};
    quotes["F"].lastPrice = "12.34";
    quotes["TSLA"].lastPrice = "380.11";
    for (PriceType type : {PriceType::Last, PriceType::Mark, PriceType::Index})
    {
        settings.priceType = type;
        calls = 0;
        Check(ValidateInstruments(settings, fetch).empty() && calls == 1,
              "stocks use last price and allow short ticker codes");
    }
    quotes.erase("TSLA");
    Check(ValidateInstruments(settings, fetch).find(L"Kraken xStocks") != std::wstring::npos,
          "missing stock identifies actual exchange");
    settings.instruments[0].symbol = L"F$";
    calls = 0;
    Check(!ValidateInstruments(settings, fetch).empty() && calls == 0, "reject invalid stock code");
    settings.instruments = {{L"USDT-FUTURES", L"BTCUSDT", 2, DataSource::Bitget},
                            {L"USDT-FUTURES", L"BTCUSDT", 2, DataSource::Binance},
                            {L"USDT-FUTURES", L"ETHUSDT", 2, DataSource::Bitget}};
    Check(!(settings.instruments[0] == settings.instruments[1]), "same symbol on different sources is not duplicate");
    int bitgetCalls = 0, binanceCalls = 0;
    settings.priceType = PriceType::Last;
    CategoryFetcher mixedFetch = [&](DataSource source, const std::wstring&, PriceType,
                                    std::unordered_map<std::string, Ticker>& tickers, std::wstring&) {
        if (source == DataSource::Bitget) ++bitgetCalls;
        else if (source == DataSource::Binance) ++binanceCalls;
        tickers["BTCUSDT"].lastPrice = source == DataSource::Bitget ? "100" : "200";
        if (source == DataSource::Bitget) tickers["ETHUSDT"].lastPrice = "300";
        return true;
    };
    Check(ValidateInstruments(settings, mixedFetch).empty() && bitgetCalls == 1 && binanceCalls == 1,
          "fetch each source/category pair once");
    settings.instruments[1].symbol = L"ETHUSDT";
    Check(!ValidateInstruments(settings, mixedFetch).empty(), "do not reuse another exchange quote for missing symbol");
    settings.instruments.clear();
    calls = 0;
    Check(ValidateInstruments(settings, fetch).empty() && calls == 0, "empty list can save without network");
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
    Check(cryptoprice::NormalizeStockSymbol(L" aapl ") == L"AAPL", "normalize stock code");
    Check(cryptoprice::NormalizeStockSymbol(L"TSLA/USD") == L"TSLA", "normalize USD stock pair");
    Check(cryptoprice::NormalizeStockSymbol(L"AAPLx/USD") == L"AAPL", "normalize xStocks pair");
    Check(cryptoprice::NormalizeStockSymbol(L"X/USD") == L"X", "retain single letter X stock");
    Check(cryptoprice::NormalizeStockSymbol(L"NFLX/USD") == L"NFLX", "retain stock code ending in X");
    Check(cryptoprice::NormalizeStockSymbol(L"NFLXx/USD") == L"NFLX", "remove only tokenization x suffix");
    Check(cryptoprice::IsValidInstrumentSymbol({L"US-STOCK", L"F"}), "allow one-letter stock");
    Check(!cryptoprice::IsValidInstrumentSymbol({L"US-STOCK", L""}), "reject empty stock");
    Check(!cryptoprice::IsValidInstrumentSymbol({L"US-STOCK", L"AAPL1"}), "reject nonletter stock code");
    Check(cryptoprice::FormatInstrumentSymbol({L"US-STOCK", L"AAPL"}) == L"AAPL/USD",
          "stock displays actual USD quote currency");
    Check(cryptoprice::DataSourceFromKey(L"okx") == cryptoprice::DataSource::Okx, "parse OKX source");
    Check(cryptoprice::DataSourceFromKey(L"binance") == cryptoprice::DataSource::Binance,
          "parse Binance source");
    Check(std::wstring(cryptoprice::DataSourceKey(cryptoprice::DataSource::Bitget)) == L"bitget",
          "serialize Bitget source");

    Check(cryptoprice::InferPricePrecision("90216.3") == 1,
          "infer 1 decimal from price string");
    Check(cryptoprice::InferPricePrecision("3100.25") == 2,
          "infer 2 decimals from price string");
    Check(cryptoprice::InferPricePrecision("0.00001234") == 8,
          "infer 8 decimals from low-price string");
    Check(cryptoprice::InferPricePrecision("80001") == 0,
          "infer 0 decimals from integer price");
    Check(cryptoprice::InferPricePrecision("") == 0, "infer empty string");
    Check(cryptoprice::InferPricePrecision("1.23456789012345") == 10,
          "clamp inferred precision to 10");

    wchar_t temporaryPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporaryPath);
    const std::filesystem::path configDirectory =
        std::filesystem::path(temporaryPath) / L"CryptoPricePluginTests";
    std::filesystem::create_directories(configDirectory);
    cryptoprice::ConfigStore config;
    config.SetDirectory(configDirectory.wstring());
    cryptoprice::Settings settings;
    settings.refreshSeconds = 30;
    settings.instruments[0].precision = 6;
    Check(config.Save(settings), "save config without credentials");
    const cryptoprice::Settings loaded = config.Load();
    Check(loaded.refreshSeconds == 30, "load refresh seconds");
    Check(loaded.instruments.size() == 1 &&
              loaded.instruments[0].symbol == L"BTCUSDT" &&
              loaded.instruments[0].precision == 6,
          "load per-instrument fixed precision");
    // 两个标的各带独立精度往返。
    cryptoprice::Settings two;
    two.instruments = {
        {L"USDT-FUTURES", L"BTCUSDT", 2},
        {L"SPOT", L"ETHUSDT", -1},
    };
    Check(config.Save(two), "save two instruments with precision");
    const cryptoprice::Settings loadedTwo = config.Load();
    Check(loadedTwo.instruments.size() == 2 &&
              loadedTwo.instruments[0].precision == 2 &&
              loadedTwo.instruments[1].precision == -1,
          "round-trip per-instrument precision");
    // 某标的 Precision 键写脏值，仅该标的回退自动（-1），其余不受影响。
    WritePrivateProfileStringW(L"Instrument0", L"Precision", L"abc",
                               config.Path().c_str());
    const cryptoprice::Settings dirty = config.Load();
    Check(dirty.instruments[0].precision == -1 &&
              dirty.instruments[1].precision == -1,
          "dirty precision key falls back to auto per instrument");
    two.instruments = {{L"US-STOCK", L"F", 2}, {L"US-STOCK", L"AAPL", -1}};
    Check(config.Save(two), "save USD stocks");
    const auto loadedStocks = config.Load();
    Check(loadedStocks.instruments.size() == 2 && loadedStocks.instruments[0].symbol == L"F" &&
          loadedStocks.instruments[0].category == L"US-STOCK" &&
          loadedStocks.instruments[0].precision == 2 && loadedStocks.instruments[1].symbol == L"AAPL",
          "round-trip stocks category, short code and precision");
    WritePrivateProfileStringW(L"General", L"DataSource", L"binance", config.Path().c_str());
    WritePrivateProfileStringW(L"Instrument0", L"Category", L"USDT-FUTURES", config.Path().c_str());
    WritePrivateProfileStringW(L"Instrument0", L"Symbol", L"BTCUSDT", config.Path().c_str());
    WritePrivateProfileStringW(L"Instrument0", L"DataSource", nullptr, config.Path().c_str());
    WritePrivateProfileStringW(L"Instrument1", L"Category", L"SPOT", config.Path().c_str());
    WritePrivateProfileStringW(L"Instrument1", L"Symbol", L"ETHUSDT", config.Path().c_str());
    WritePrivateProfileStringW(L"Instrument1", L"DataSource", L"okx", config.Path().c_str());
    const auto migratedSources = config.Load();
    Check(migratedSources.instruments.size() == 2 &&
          migratedSources.instruments[0].dataSource == cryptoprice::DataSource::Binance &&
          migratedSources.instruments[1].dataSource == cryptoprice::DataSource::Okx,
          "legacy source migrates only for instruments lacking their own key");
    Check(config.Save(migratedSources), "save migrated per-instrument sources");
    wchar_t globalSource[32]{};
    GetPrivateProfileStringW(L"General", L"DataSource", L"missing", globalSource, 32, config.Path().c_str());
    Check(std::wstring(globalSource) == L"missing", "remove obsolete global source after save");
    const auto reloadedSources = config.Load();
    Check(reloadedSources.instruments[0].dataSource == cryptoprice::DataSource::Binance &&
          reloadedSources.instruments[1].dataSource == cryptoprice::DataSource::Okx,
          "per-instrument sources survive reload without legacy key");
    std::filesystem::remove_all(configDirectory);

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

    const std::string stocks = R"({"error":[],"result":{
      "AAPLxUSD":{"c":["352.81","0.692700"],"o":"353.33"},
      "FxUSD":{"c":["12.34","1"],"o":"12"},
      "TSLAxUSD":{"c":["380.11","1"],"v":["1","2"]},
      "AAPLSPVUSD":{"c":["354.16","1"]},
      "AAPLxEUR":{"c":["300","1"]},
      "AAPLxUSDT":{"c":["353","1"]}
    }})";
    Check(cryptoprice::ParseKrakenStockResponse(stocks, tickers, error), "parse Kraken stocks");
    Check(tickers.size() == 3 && tickers.at("AAPL").lastPrice == "352.81" &&
          tickers.at("F").lastPrice == "12.34" && tickers.at("TSLA").lastPrice == "380.11",
          "only canonical xUSD pairs, not SPV aliases, EUR or USDT");
    Check(tickers.at("AAPL").price24hPcnt.empty() && tickers.at("AAPL").markPrice.empty(),
          "do not mislabel daily change as rolling 24h change or invent derivative prices");
    for (const std::string invalid : {
        R"({"error":["EQuery:Unknown asset pair"],"result":{}})",
        R"({"result":{}})", R"({"error":[],"result":{}})",
        R"({"error":[],"result":{"AAPLxUSD":{"c":[]}}})",
        R"({"error":[],"result":{"AAPLxUSD":{"c":["1","1"]}})",
        R"({"error":[],"result":{"AAPLxUSD":{"c":["1","1"]},}})"})
        Check(!cryptoprice::ParseKrakenStockResponse(invalid, tickers, error) && tickers.empty(),
              "reject failed or malformed stock response without partial tickers");

    Check(cryptoprice::BuildCompactTooltip(L"行情", {L"AAPL/USD: 100.00", L"TSLA/USD: 200.00"}) ==
          L"行情\nAAPL/USD: 100.00\nTSLA/USD: 200.00", "short tooltip retains complete rows");
    const auto longTooltip = cryptoprice::BuildCompactTooltip(L"行情", std::vector<std::wstring>(16, std::wstring(100, L'X')));
    Check(longTooltip.size() <= cryptoprice::kMaxTooltipLength &&
          longTooltip.find(L"更多见插件设置") != std::wstring::npos, "long tooltip is bounded and marked");
    Check(cryptoprice::BuildCompactTooltip(std::wstring(2000, L'X'), {}).size() <=
          cryptoprice::kMaxTooltipLength, "long title is bounded");
    // The failing user's other plugins and host consume 514 characters. Keep a
    // conservative 600-character co-resident budget below the MFC 1024 limit.
    Check(longTooltip.size() + 600 <= 1024, "leave host and co-resident plugin tooltip headroom");

    TestInstrumentValidation();

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
