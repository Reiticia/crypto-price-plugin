#include "Config.h"
#include "Json.h"
#include "InstrumentValidation.h"
#include <cmath>
#include <iostream>

int main()
{
    using namespace cryptoprice;
    int failures = 0;
    const auto check = [&](bool valid, const char* message) {
        if (!valid) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
    };
    std::unordered_map<std::string, Ticker> tickers;
    std::string error;
    const std::string pairs = R"({"error":[],"result":{
      "XXBTZUSD":{"altname":"XBTUSD","wsname":"XBT/USD","aclass_base":"currency","status":"online"},
      "XBTUSDT":{"altname":"XBTUSDT","wsname":"XBT/USDT","aclass_base":"currency","status":"online"},
      "XXDGZUSD":{"altname":"XDGUSD","wsname":"XDG/USD","aclass_base":"currency","status":"online"},
      "AAPLxUSD":{"wsname":"AAPLx/USD","aclass_base":"tokenized_asset","status":"online"}}})";
    const std::string spot = R"({"error":[],"result":{
      "XXBTZUSD":{"c":["100.12","1"],"o":"90"},"XBTUSDT":{"c":["101","2"]},
      "XXDGZUSD":{"c":["0.12","1"]},"AAPLxUSD":{"c":["300","1"]}}})";
    check(ParseKrakenSpotResponse(pairs, spot, tickers, error), "Kraken spot parses metadata and ticker");
    check(tickers.size() == 3 && tickers.at("BTCUSD").lastPrice == "100.12" &&
          tickers.count("BTCUSDT") && tickers.count("DOGEUSD"), "Kraken XBT/XDG normalize without USD-USDT confusion");
    check(tickers.at("BTCUSD").price24hPcnt.empty(), "Kraken today open is not a rolling 24h change");
    const std::string futures = R"({"result":"success","tickers":[
      {"symbol":"PF_XBTUSD","tag":"perpetual","pair":"XBT:USD","last":110.25,"markPrice":110.2,"indexPrice":110,"open24h":100,"suspended":false},
      {"symbol":"PI_XBTUSD","tag":"perpetual","pair":"XBT:USD","last":999,"suspended":false},
      {"symbol":"FI_XBTUSD_260101","tag":"month","pair":"XBT:USD","last":999,"suspended":false},
      {"symbol":"PF_ETHUSD","tag":"perpetual","pair":"ETH:USD","last":999,"suspended":true}]})";
    check(ParseKrakenFuturesResponse(futures, tickers, error), "Kraken USD perpetual parses numeric fields");
    check(tickers.size() == 1 && tickers.at("BTCUSD").markPrice == "110.2" &&
          std::abs(std::stod(tickers.at("BTCUSD").price24hPcnt) - 0.1025) < 1e-10,
          "Kraken excludes inverse, dated and suspended products and uses open24h");
    const std::string instruments = R"({"retCode":0,"result":{"category":"linear","nextPageCursor":"next=abc","list":[
      {"symbol":"BTCUSDT","baseCoin":"BTC","quoteCoin":"USDT","status":"Trading","contractType":"LinearPerpetual"},
      {"symbol":"BTCPERP","baseCoin":"BTC","quoteCoin":"USDC","status":"Trading","contractType":"LinearPerpetual"},
      {"symbol":"BTCUSDT-260101","baseCoin":"BTC","quoteCoin":"USDT","status":"Trading","contractType":"LinearFutures"},
      {"symbol":"ETHUSDT","baseCoin":"ETH","quoteCoin":"USDT","status":"PreLaunch","contractType":"LinearPerpetual"}]}})";
    const std::string bybit = R"({"retCode":0,"result":{"category":"linear","list":[
      {"symbol":"BTCUSDT","lastPrice":"100","markPrice":"101","indexPrice":"102","price24hPcnt":"0.05"},
      {"symbol":"BTCPERP","lastPrice":"200","markPrice":"201","indexPrice":"202","price24hPcnt":"-0.01"},
      {"symbol":"BTCUSDT-260101","lastPrice":"999"},{"symbol":"ETHUSDT","lastPrice":"999"}]}})";
    check(ParseBybitResponse({instruments}, bybit, L"USDT-FUTURES", tickers, error), "Bybit USDT perpetual");
    check(tickers.size() == 1 && tickers.at("BTCUSDT").price24hPcnt == "0.05", "Bybit fraction not divided by 100 or mixed with dated futures");
    check(ParseBybitResponse({instruments}, bybit, L"USDC-FUTURES", tickers, error), "Bybit USDC perpetual");
    check(tickers.size() == 1 && tickers.at("BTCUSDC").lastPrice == "200", "BTCPERP maps only to BTCUSDC");
    const std::string inverseInfo = R"({"retCode":0,"result":{"category":"inverse","list":[
      {"symbol":"BTCUSD","baseCoin":"BTC","quoteCoin":"USD","status":"Trading","contractType":"InversePerpetual"},
      {"symbol":"BTCUSDZ26","baseCoin":"BTC","quoteCoin":"USD","status":"Trading","contractType":"InverseFutures"}]}})";
    const std::string inverseTickers = R"({"retCode":0,"result":{"category":"inverse","list":[
      {"symbol":"BTCUSD","lastPrice":"123","markPrice":"124","indexPrice":"125"},{"symbol":"BTCUSDZ26","lastPrice":"999"}]}})";
    check(ParseBybitResponse({inverseInfo}, inverseTickers, L"COIN-FUTURES", tickers, error) &&
          tickers.size() == 1 && tickers.at("BTCUSD").lastPrice == "123", "Bybit inverse excludes delivery futures");
    const std::string spotInfo = R"({"retCode":0,"result":{"category":"spot","list":[
      {"symbol":"BTCUSDT","baseCoin":"BTC","quoteCoin":"USDT","status":"Trading"}]}})";
    const std::string spotTickers = R"({"retCode":0,"result":{"category":"spot","list":[
      {"symbol":"BTCUSDT","lastPrice":"123","price24hPcnt":"-0.02"}]}})";
    check(ParseBybitResponse({spotInfo}, spotTickers, L"SPOT", tickers, error) &&
          tickers.at("BTCUSDT").markPrice.empty(), "Bybit spot retains last and does not invent mark");
    const std::string tiny = R"({"result":"success","tickers":[{"symbol":"PF_TINYUSD","pair":"TINY:USD",
      "tag":"perpetual","suspended":false,"last":1.23e-7,"markPrice":1.24e-7,"indexPrice":1.25e-7}]})";
    check(ParseKrakenFuturesResponse(tiny, tickers, error) && tickers.at("TINYUSD").lastPrice == "0.000000123" &&
          InferPricePrecision(tickers.at("TINYUSD").lastPrice) == 9, "numeric exponent prices preserve useful precision");
    std::string cursor;
    check(BybitNextCursor(instruments, cursor, error) && cursor == "next=abc", "Bybit pagination cursor");
    for (const std::string bad : {std::string(R"({"retCode":10001,"retMsg":"bad parameter","result":{"list":[]}})"),
                                  std::string("{}"), bybit.substr(0, bybit.size()-1), bybit + "junk"})
        check(!ParseBybitResponse({instruments}, bad, L"USDT-FUTURES", tickers, error) && tickers.empty(), "Bybit errors and malformed JSON fail closed");
    check(!ParseKrakenSpotResponse(pairs, R"({"error":["failure"],"result":{}})", tickers, error), "Kraken API error");
    check(IsSupportedMarket(DataSource::Kraken, L"USD-FUTURES") && !IsSupportedMarket(DataSource::Kraken, L"USDT-FUTURES"), "Kraken USD contracts are not mislabeled USDT");
    check(Supports24HourChange({L"SPOT", L"BTCUSD", 2, DataSource::Kraken}) &&
          Supports24HourChange({L"USD-FUTURES", L"BTCUSD", 2, DataSource::Kraken}),
          "Kraken spot WebSocket change_pct and futures change both support rolling change");
    check(FormatInstrumentSymbol({L"SPOT", L"BTCUSD", 2, DataSource::Kraken}) == L"BTC/USD",
          "USD spot display keeps currency separator");
    check(DataSourceFromKey(L"bybit") == DataSource::Bybit && DataSourceFromKey(L"kraken") == DataSource::Kraken, "new source keys");
    for (DataSource source : {DataSource::Bybit, DataSource::Kraken})
    {
        Settings settings;
        settings.instruments = {{source == DataSource::Kraken ? L"USD-FUTURES" : L"USDC-FUTURES",
                                 source == DataSource::Kraken ? L"BTCUSD" : L"BTCUSDC", 2, source}};
        check(ValidateInstruments(settings, [&](DataSource s, const std::wstring&, PriceType,
              std::unordered_map<std::string, Ticker>& quotes, std::wstring&) {
            check(s == source, "validation routes selected exchange");
            quotes[source == DataSource::Kraken ? "BTCUSD" : "BTCUSDC"].lastPrice = "100";
            return true;
        }).empty(), "new exchange instrument validation");
    }
    return failures ? 1 : 0;
}
