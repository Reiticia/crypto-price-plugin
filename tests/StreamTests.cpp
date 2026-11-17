#include "StreamProtocol.h"
#include "StreamSnapshots.h"
#include <cmath>
#include <iostream>

int main()
{
    using namespace cryptoprice;
    int failures{};
    const auto check = [&](bool ok, const char* text) { if (!ok) { ++failures; std::cerr << "FAILED: " << text << '\n'; } };
    const auto planFor = [](DataSource source, const std::wstring& category, const std::wstring& symbol,
                            const std::vector<std::string>& metadata = {}) {
        return BuildStreamPlan(source, category, {{category, symbol, 2, source}}, metadata);
    };
    const std::string info = R"({"retCode":0,"result":{"category":"linear","list":[
      {"symbol":"BTCPERP","baseCoin":"BTC","quoteCoin":"USDC","status":"Trading","contractType":"LinearPerpetual"},
      {"symbol":"BTCUSDT-261225","baseCoin":"BTC","quoteCoin":"USDT","status":"Trading","contractType":"LinearFutures"}]}})";
    auto bybit = planFor(DataSource::Bybit, L"USDC-FUTURES", L"BTCUSDC", {info});
    check(bybit.host == L"stream.bybit.com" && bybit.path == L"/v5/public/linear" &&
          bybit.subscriptions[0].find("tickers.BTCPERP") != std::string::npos, "Bybit wire symbol derived from metadata");
    std::map<std::string, Ticker> cache;
    ApplyStreamMessage(bybit, R"({"topic":"tickers.BTCPERP","type":"snapshot","data":{"symbol":"BTCPERP","lastPrice":"100","markPrice":"101","indexPrice":"102","price24hPcnt":"0.01"}})", cache);
    ApplyStreamMessage(bybit, R"({"topic":"tickers.BTCPERP","type":"delta","data":{"markPrice":"103"}})", cache);
    check(cache.at("BTCUSDC").lastPrice == "100" && cache.at("BTCUSDC").indexPrice == "102" &&
          cache.at("BTCUSDC").markPrice == "103" && cache.at("BTCUSDC").price24hPcnt == "0.01",
          "Bybit omitted delta fields retain snapshot state");
    ApplyStreamMessage(bybit, R"({"topic":"tickers.BTCPERP","type":"snapshot","data":{"lastPrice":"200"}})", cache);
    check(cache.at("BTCUSDC").markPrice.empty(), "Bybit new snapshot clears old fields");
    const auto before = cache;
    check(ApplyStreamMessage(bybit, R"({"op":"pong","success":true})", cache).empty() && cache.size() == before.size(), "pong never overwrites quote");
    bool rejected{};
    try { ApplyStreamMessage(bybit, R"({"op":"subscribe","success":false,"ret_msg":"invalid symbol"})", cache); }
    catch (...) { rejected = true; }
    check(rejected, "subscription failures surface as reconnect errors");
    rejected = false;
    try { planFor(DataSource::Bybit, L"USDT-FUTURES", L"BTCUSDT", {info}); } catch (...) { rejected = true; }
    check(rejected, "dated future metadata cannot satisfy perpetual subscription");

    auto okx = planFor(DataSource::Okx, L"USDT-FUTURES", L"BTCUSDT");
    check(okx.port == 443 && okx.subscriptions[0].find("BTC-USDT-SWAP") != std::string::npos &&
          okx.subscriptions[0].find("index-tickers") != std::string::npos, "OKX port 443 plus three price channels");
    cache.clear();
    ApplyStreamMessage(okx, R"({"arg":{"channel":"tickers","instId":"BTC-USDT-SWAP"},"data":[{"last":"100","open24h":"80"}]})", cache);
    ApplyStreamMessage(okx, R"({"arg":{"channel":"mark-price","instId":"BTC-USDT-SWAP"},"data":[{"markPx":"101"}]})", cache);
    ApplyStreamMessage(okx, R"({"arg":{"channel":"index-tickers","instId":"BTC-USDT"},"data":[{"idxPx":"102"}]})", cache);
    check(cache.at("BTCUSDT").lastPrice == "100" && cache.at("BTCUSDT").markPrice == "101" &&
          cache.at("BTCUSDT").indexPrice == "102" && std::stod(cache.at("BTCUSDT").price24hPcnt) == 0.25,
          "OKX channels merge instead of resetting each other");

    auto binance = planFor(DataSource::Binance, L"COIN-FUTURES", L"BTCUSD");
    check(binance.path == L"/market/ws" && binance.subscriptions[0].find("btcusd_perp@markPrice@1s") != std::string::npos,
          "Binance derivatives use market routing and coin perpetual symbol");
    cache.clear();
    ApplyStreamMessage(binance, R"({"e":"24hrTicker","s":"BTCUSD_PERP","c":"100","P":"2.5"})", cache);
    ApplyStreamMessage(binance, R"({"e":"markPriceUpdate","s":"BTCUSD_PERP","p":"101","i":"102"})", cache);
    check(cache.at("BTCUSD").lastPrice == "100" && cache.at("BTCUSD").markPrice == "101" &&
          std::abs(std::stod(cache.at("BTCUSD").price24hPcnt) - 0.025) < 1e-10, "Binance separate event types and percentage scaling");
    auto bitget = planFor(DataSource::Bitget, L"USDT-FUTURES", L"BTCUSDT");
    check(bitget.path == L"/v3/ws/public" && bitget.subscriptions[0].find("\"topic\":\"ticker\"") != std::string::npos,
          "Bitget UTA v3 ticker protocol");
    cache.clear();
    ApplyStreamMessage(bitget, R"({"arg":{"symbol":"BTCUSDT","topic":"ticker"},"action":"snapshot","data":[{"lastPrice":"100","markPrice":"101","indexPrice":"102","price24hPcnt":"0.05"}]})", cache);
    check(cache.at("BTCUSDT").price24hPcnt == "0.05" && cache.at("BTCUSDT").indexPrice == "102", "Bitget snapshot fields");

    const std::string pairs = R"({"error":[],"result":{"XXBTZUSD":{"wsname":"XBT/USD","status":"online","aclass_base":"currency"},
      "AAPLxUSD":{"wsname":"AAPLx/USD","status":"online","aclass_base":"tokenized_asset"},
      "AAPLSPVUSD":{"wsname":"AAPLx/USD","status":"online","aclass_base":"tokenized_asset"}}})";
    auto kraken = planFor(DataSource::Kraken, L"SPOT", L"BTCUSD", {pairs});
    check(kraken.path == L"/v2" && kraken.subscriptions[0].find("BTC/USD") != std::string::npos, "Kraken v2 BTC not REST XBT");
    cache.clear();
    ApplyStreamMessage(kraken, R"({"channel":"ticker","type":"snapshot","data":[{"symbol":"BTC/USD","last":100.5,"change_pct":2.5}]})", cache);
    check(cache.at("BTCUSD").lastPrice == "100.5" && std::abs(std::stod(cache.at("BTCUSD").price24hPcnt)-0.025)<1e-10, "Kraken v2 numbers and real change_pct");
    auto stock = planFor(DataSource::Kraken, L"US-STOCK", L"AAPL", {pairs});
    check(stock.symbols.size() == 1 && stock.subscriptions[0].find("AAPLx/USD") != std::string::npos, "Kraken xStocks subscribes actual USD token symbol");
    cache.clear();
    ApplyStreamMessage(stock, R"({"channel":"ticker","type":"snapshot","data":[{"symbol":"AAPLx/USD","last":352.5,"change_pct":1}]})", cache);
    check(cache.at("AAPL").lastPrice == "352.5" && cache.at("AAPL").price24hPcnt.empty(), "stock does not invent a conventional stock change");
    auto futures = planFor(DataSource::Kraken, L"USD-FUTURES", L"BTCUSD",
        {R"({"result":"success","instruments":[{"symbol":"PF_XBTUSD","base":"BTC","quote":"USD","tradeable":true,"suspended":false}]})"});
    check(futures.subscriptions[0].find("PF_XBTUSD") != std::string::npos, "Kraken Futures PF wire symbol");
    cache.clear();
    ApplyStreamMessage(futures, R"({"feed":"ticker","product_id":"PF_XBTUSD","last":100,"markPrice":101,"index":102,"change":-2})", cache);
    check(cache.at("BTCUSD").indexPrice == "102" && std::stod(cache.at("BTCUSD").price24hPcnt) == -0.02, "Kraken Futures ticker fields");
    cache.clear(); // Every connection starts fresh, never reusing the previous generation.
    ApplyStreamMessage(bybit, R"({"topic":"tickers.BTCPERP","type":"delta","data":{"markPrice":"103"}})", cache);
    check(cache.empty(), "delta before reconnect snapshot is ignored");
    rejected = false;
    try { ApplyStreamMessage(bybit, "{broken", cache); } catch (...) { rejected = true; }
    check(rejected, "malformed messages do not silently overwrite cache");
    StreamSnapshots snapshots;
    const std::vector<std::size_t> indexes{2, 5, 9};
    snapshots.Observe(2);
    check(snapshots.Missing(indexes, std::chrono::seconds(30)).empty(), "missing snapshot grace period");
    snapshots.Observe(5); // A snapshot in the timeout-crossing frame is processed before warning.
    check(snapshots.Missing(indexes, std::chrono::seconds(31)) == std::vector<std::size_t>{9},
          "only actually missing indexes are warned");
    check(snapshots.Missing(indexes, std::chrono::seconds(32)).empty() &&
          snapshots.Missing(indexes, std::chrono::seconds(90)).empty(),
          "repeated heartbeat and price frames do not repeat missing warnings");
    snapshots.Observe(9);
    check(snapshots.Missing(indexes, std::chrono::seconds(91)).empty(), "late snapshot resolves missing instrument");
    StreamSnapshots reconnected;
    check(reconnected.Missing(indexes, std::chrono::seconds(31)) == indexes,
          "new connection resets both seen and warned indexes");
    return failures ? 1 : 0;
}
