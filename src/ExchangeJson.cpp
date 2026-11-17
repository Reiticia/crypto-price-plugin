#include "Json.h"
#include "JsonValue.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace cryptoprice
{
namespace
{
using json::Value;
using json::Parser;
std::string First(const Value& value)
{
    return value.kind == '[' && !value.array.empty() ? value.array.front().Text() : "";
}
std::string Coin(std::string coin) { if (coin == "XBT") return "BTC"; if (coin == "XDG") return "DOGE"; return coin; }
std::string Fraction(const std::string& last, const std::string& open)
{
    char *a{}, *b{};
    double l = std::strtod(last.c_str(), &a), o = std::strtod(open.c_str(), &b);
    if (a == last.c_str() || *a || b == open.c_str() || *b || !std::isfinite(l) || !std::isfinite(o) || o <= 0) return "";
    std::ostringstream output;
    output << std::setprecision(16) << (l - o) / o;
    return output.str();
}
std::string Pair(const std::string& display, char separator)
{
    const auto split = display.find(separator);
    if (split == std::string::npos) return "";
    return Coin(display.substr(0, split)) + Coin(display.substr(split + 1));
}
void KrakenOk(const Value& root)
{
    const Value& errors = root.Get("error");
    if (errors.kind != '[' || !errors.array.empty()) throw std::runtime_error("Kraken API error");
}
const Value& BybitList(const Value& root)
{
    if (root.Get("retCode").Text() != "0") throw std::runtime_error("Bybit: " + root.Text("retMsg"));
    const Value& list = root.Get("result").Get("list");
    if (list.kind != '[') throw std::runtime_error("Missing Bybit list");
    return list;
}
template<typename Function> bool ParseQuotes(std::unordered_map<std::string, Ticker>& tickers, std::string& error, Function fn)
{
    tickers.clear();
    try { fn(); if (tickers.empty()) throw std::runtime_error("No supported ticker returned"); error.clear(); return true; }
    catch (const std::exception& ex) { tickers.clear(); error = ex.what(); return false; }
}
}

bool ParseKrakenSpotResponse(const std::string& pairsJson, const std::string& tickerJson,
                            std::unordered_map<std::string, Ticker>& tickers, std::string& error)
{
    return ParseQuotes(tickers, error, [&] {
        const auto pairs = Parser(pairsJson).Parse(), quotes = Parser(tickerJson).Parse();
        KrakenOk(pairs); KrakenOk(quotes);
        const Value& results = quotes.Get("result");
        for (const auto& [key, info] : pairs.Get("result").object)
        {
            if (info.Text("aclass_base") != "currency" || info.Text("status") != "online") continue;
            const std::string symbol = Pair(info.Text("wsname"), '/');
            auto found = results.object.find(key);
            if (found == results.object.end()) found = results.object.find(info.Text("altname"));
            if (symbol.empty() || found == results.object.end()) continue;
            Ticker ticker; ticker.symbol = symbol; ticker.lastPrice = First(found->second.Get("c"));
            if (!ticker.lastPrice.empty()) tickers[symbol] = std::move(ticker);
        }
    });
}

bool ParseKrakenFuturesResponse(const std::string& json,
                               std::unordered_map<std::string, Ticker>& tickers, std::string& error)
{
    return ParseQuotes(tickers, error, [&] {
        const auto root = Parser(json).Parse();
        if (root.Text("result") != "success") throw std::runtime_error("Kraken Futures API error");
        const auto& list = root.Get("tickers");
        if (list.kind != '[') throw std::runtime_error("Missing Kraken Futures tickers");
        for (const auto& item : list.array)
        {
            const std::string raw = item.Text("symbol");
            if (raw.rfind("PF_", 0) != 0 || item.Text("tag") != "perpetual" || item.Get("suspended").scalar != "false") continue;
            const std::string symbol = Pair(item.Text("pair"), ':');
            if (symbol.size() <= 3 || symbol.substr(symbol.size() - 3) != "USD") continue;
            Ticker ticker; ticker.symbol = symbol;
            ticker.lastPrice = item.Text("last"); ticker.markPrice = item.Text("markPrice"); ticker.indexPrice = item.Text("indexPrice");
            ticker.price24hPcnt = Fraction(ticker.lastPrice, item.Text("open24h"));
            ticker.timestamp = item.Text("lastTime");
            tickers[symbol] = std::move(ticker);
        }
    });
}

bool ParseBybitResponse(const std::vector<std::string>& instruments, const std::string& json,
                        const std::wstring& category, std::unordered_map<std::string, Ticker>& tickers, std::string& error)
{
    return ParseQuotes(tickers, error, [&] {
        std::map<std::string, std::string> symbols;
        const bool spot = category == L"SPOT", inverse = category == L"COIN-FUTURES";
        const std::string apiCategory = spot ? "spot" : inverse ? "inverse" : "linear";
        const std::string quote = category == L"USDC-FUTURES" ? "USDC" : inverse ? "USD" : "USDT";
        for (const std::string& page : instruments)
        {
            const auto root = Parser(page).Parse();
            if (root.Get("result").Text("category") != apiCategory) throw std::runtime_error("Bybit instrument category mismatch");
            for (const auto& item : BybitList(root).array)
            {
                if (item.Text("status") != "Trading") continue;
                if (!spot && (item.Text("contractType") != (inverse ? "InversePerpetual" : "LinearPerpetual") || item.Text("quoteCoin") != quote)) continue;
                const std::string raw = item.Text("symbol");
                if (!raw.empty()) symbols[raw] = item.Text("baseCoin") + item.Text("quoteCoin");
            }
        }
        const auto root = Parser(json).Parse();
        if (root.Get("result").Text("category") != apiCategory) throw std::runtime_error("Bybit ticker category mismatch");
        for (const auto& item : BybitList(root).array)
        {
            const auto found = symbols.find(item.Text("symbol"));
            if (found == symbols.end() || found->second.empty()) continue;
            Ticker ticker; ticker.symbol = found->second;
            ticker.lastPrice = item.Text("lastPrice"); ticker.markPrice = item.Text("markPrice"); ticker.indexPrice = item.Text("indexPrice");
            ticker.price24hPcnt = item.Text("price24hPcnt");
            tickers[ticker.symbol] = std::move(ticker);
        }
    });
}

bool BybitNextCursor(const std::string& json, std::string& cursor, std::string& error)
{
    cursor.clear();
    try { const auto root = Parser(json).Parse(); BybitList(root); cursor = root.Get("result").Text("nextPageCursor"); error.clear(); return true; }
    catch (const std::exception& ex) { error = ex.what(); return false; }
}
} // namespace cryptoprice
