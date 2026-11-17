#include "StreamProtocol.h"
#include "JsonValue.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <stdexcept>

namespace cryptoprice
{
namespace
{
using json::Parser;
using json::Value;
std::string Ascii(const std::wstring& text)
{
    std::string result;
    for (wchar_t ch : text)
    {
        if (ch > 127) throw std::runtime_error("Non-ASCII exchange symbol");
        result += static_cast<char>(ch);
    }
    return result;
}
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}
std::string Coin(std::string text) { return text == "XBT" ? "BTC" : text == "XDG" ? "DOGE" : text; }
std::string Join(const std::vector<std::string>& parts)
{
    std::string result;
    for (const auto& part : parts) { if (!result.empty()) result += ','; result += part; }
    return result;
}
std::string PercentFraction(const std::string& text)
{
    char* end{};
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(value)) return "";
    std::ostringstream output; output << std::setprecision(16) << value / 100.0;
    return output.str();
}
std::string Fraction(const std::string& last, const std::string& open)
{
    char *a{}, *b{};
    const double l = std::strtod(last.c_str(), &a), o = std::strtod(open.c_str(), &b);
    if (a == last.c_str() || *a || b == open.c_str() || *b || !std::isfinite(l) || !std::isfinite(o) || o <= 0) return "";
    std::ostringstream output; output << std::setprecision(16) << (l - o) / o; return output.str();
}
std::string SlashPair(const std::string& text)
{
    const auto split = text.find('/');
    return split == std::string::npos ? "" : Coin(text.substr(0, split)) + Coin(text.substr(split + 1));
}
bool Spot(const StreamPlan& p) { return p.category == L"SPOT" || p.category == L"US-STOCK"; }
void Patch(const Value& value, const char* field, std::string& destination)
{
    if (value.object.count(field)) destination = value.Get(field).Text();
}
void CheckError(const Value& root)
{
    if ((root.object.count("success") && root.Get("success").scalar == "false") ||
        root.Text("event") == "error" || root.Text("event") == "subscribed_failed" ||
        (!root.Text("code").empty() && root.Text("code") != "0" && root.Text("code") != "00000") ||
        (!root.Text("retCode").empty() && root.Text("retCode") != "0") ||
        root.object.count("error"))
        throw std::runtime_error("WebSocket subscription error: " + root.Text("msg") + root.Text("ret_msg") +
                                 root.Text("retMsg") + root.Text("error") + root.Text("message"));
}
}
std::string JsonQuote(const std::string& text)
{
    std::string result = "\"";
    for (unsigned char ch : text)
    {
        if (ch < 32) throw std::runtime_error("Control character in subscription identifier");
        if (ch == '"' || ch == '\\') result += '\\';
        result += static_cast<char>(ch);
    }
    return result + '"';
}

StreamPlan BuildStreamPlan(DataSource source, const std::wstring& category,
                           const std::vector<Instrument>& instruments,
                           const std::vector<std::string>& metadata)
{
    if (!IsSupportedMarket(source, category)) throw std::runtime_error("Unsupported exchange/market combination");
    StreamPlan plan; plan.source = source; plan.category = category;
    std::map<std::string, std::string> candidates;
    if (source == DataSource::Kraken && category != L"USD-FUTURES")
    {
        if (metadata.empty()) throw std::runtime_error("Kraken AssetPairs metadata required");
        const auto root = Parser(metadata.front()).Parse();
        if (!root.Get("error").array.empty()) throw std::runtime_error("Kraken metadata error");
        for (const auto& [key, info] : root.Get("result").object)
        {
            (void)key;
            const std::string ws = info.Text("wsname");
            const bool stock = category == L"US-STOCK";
            if (info.Text("status") != "online" || info.Text("aclass_base") != (stock ? "tokenized_asset" : "currency")) continue;
            if (stock)
            {
                const auto split = ws.find("x/USD");
                if (split != std::string::npos && split + 5 == ws.size()) candidates[ws.substr(0, split)] = ws;
            }
            else
            {
                std::string wire = ws;
                const auto split = ws.find('/');
                if (split != std::string::npos) wire = Coin(ws.substr(0, split)) + "/" + Coin(ws.substr(split + 1));
                candidates[SlashPair(ws)] = wire;
            }
        }
    }
    if (source == DataSource::Kraken && category == L"USD-FUTURES")
    {
        if (metadata.empty()) throw std::runtime_error("Kraken Futures instrument metadata required");
        const auto root = Parser(metadata.front()).Parse();
        if (root.Text("result") != "success") throw std::runtime_error("Kraken Futures metadata error");
        for (const auto& item : root.Get("instruments").array)
        {
            const std::string wire = item.Text("symbol");
            if (wire.rfind("PF_", 0) != 0 || item.Get("tradeable").scalar != "true" ||
                item.Get("suspended").scalar != "false" || item.Text("quote") != "USD") continue;
            candidates[Coin(item.Text("base")) + "USD"] = wire;
        }
    }
    if (source == DataSource::Bybit)
    {
        const std::string expected = category == L"SPOT" ? "spot" : category == L"COIN-FUTURES" ? "inverse" : "linear";
        for (const auto& page : metadata)
        {
            const auto root = Parser(page).Parse();
            if (root.Get("retCode").Text() != "0" || root.Get("result").Text("category") != expected)
                throw std::runtime_error("Bybit metadata error");
            for (const auto& info : root.Get("result").Get("list").array)
            {
                if (info.Text("status") != "Trading") continue;
                if (category != L"SPOT")
                {
                    const std::string quote = category == L"USDC-FUTURES" ? "USDC" : category == L"COIN-FUTURES" ? "USD" : "USDT";
                    if (info.Text("quoteCoin") != quote || info.Text("contractType") !=
                        (category == L"COIN-FUTURES" ? "InversePerpetual" : "LinearPerpetual")) continue;
                }
                candidates[info.Text("baseCoin") + info.Text("quoteCoin")] = info.Text("symbol");
            }
        }
    }
    std::vector<std::string> args;
    for (const auto& instrument : instruments)
    {
        const std::string configured = Ascii(instrument.symbol);
        std::string wire = configured;
        if (source == DataSource::Kraken || source == DataSource::Bybit)
        {
            {
                const auto found = candidates.find(configured);
                if (found == candidates.end()) throw std::runtime_error("No exchange instrument metadata for " + configured);
                wire = found->second;
            }
        }
        else if (source == DataSource::Okx)
        {
            std::string quote;
            for (const char* q : {"FDUSD", "USDT", "USDC", "TUSD", "USD", "BTC", "ETH", "EUR", "GBP", "DAI", "BRL", "TRY"})
            {
                const std::string suffix = q;
                if (configured.size() > suffix.size() && configured.compare(configured.size()-suffix.size(), suffix.size(), suffix) == 0)
                { quote = suffix; break; }
            }
            if (quote.empty()) throw std::runtime_error("Cannot resolve OKX quote currency");
            wire = configured.substr(0, configured.size()-quote.size()) + "-" + quote;
            if (!Spot(plan)) wire += "-SWAP";
        }
        else if (source == DataSource::Binance && category == L"COIN-FUTURES") wire += "_PERP";
        if (!plan.symbols.emplace(wire, configured).second) continue;
        if (source == DataSource::Bybit) args.push_back(JsonQuote("tickers." + wire));
        else if (source == DataSource::Kraken) args.push_back(JsonQuote(wire));
        else if (source == DataSource::Bitget)
            args.push_back("{\"instType\":" + JsonQuote(Lower(Ascii(category))) + ",\"topic\":\"ticker\",\"symbol\":" + JsonQuote(wire) + "}");
        else if (source == DataSource::Okx)
        {
            args.push_back("{\"channel\":\"tickers\",\"instId\":" + JsonQuote(wire) + "}");
            if (!Spot(plan))
            {
                args.push_back("{\"channel\":\"mark-price\",\"instId\":" + JsonQuote(wire) + "}");
                const std::string index = wire.substr(0, wire.size()-5);
                args.push_back("{\"channel\":\"index-tickers\",\"instId\":" + JsonQuote(index) + "}");
                plan.symbols[index] = configured;
            }
        }
        else
        {
            args.push_back(JsonQuote(Lower(wire) + "@ticker"));
            if (!Spot(plan)) args.push_back(JsonQuote(Lower(wire) + "@markPrice@1s"));
        }
    }
    if (args.empty()) throw std::runtime_error("Empty WebSocket subscription");
    if (source == DataSource::Kraken)
    {
        if (category == L"USD-FUTURES")
        {
            plan.host = L"futures.kraken.com"; plan.path = L"/ws/v1";
            plan.subscriptions = {"{\"event\":\"subscribe\",\"feed\":\"ticker\",\"product_ids\":[" + Join(args) + "]}",
                                  "{\"event\":\"subscribe\",\"feed\":\"heartbeat\"}"};
            // Futures supplies its subscribed heartbeat; WinHTTP handles control ping/pong.
            plan.heartbeat.clear();
        }
        else
        {
            plan.host = L"ws.kraken.com"; plan.path = L"/v2";
            plan.subscriptions = {"{\"method\":\"subscribe\",\"params\":{\"channel\":\"ticker\",\"symbol\":[" + Join(args) + "],\"snapshot\":true,\"event_trigger\":\"trades\"}}"};
            plan.heartbeat = "{\"method\":\"ping\"}";
        }
    }
    else if (source == DataSource::Bybit)
    {
        plan.host = L"stream.bybit.com"; plan.path = category == L"SPOT" ? L"/v5/public/spot" : category == L"COIN-FUTURES" ? L"/v5/public/inverse" : L"/v5/public/linear";
        // Spot accepts at most ten args per subscription request.
        for (std::size_t start = 0; start < args.size(); start += 10)
        {
            const auto end = (std::min)(start + 10, args.size());
            plan.subscriptions.push_back("{\"op\":\"subscribe\",\"args\":[" + Join({args.begin()+start, args.begin()+end}) + "]}");
        }
        plan.heartbeat = "{\"op\":\"ping\"}";
    }
    else if (source == DataSource::Bitget)
    {
        plan.host = L"ws.bitget.com"; plan.path = L"/v3/ws/public";
        plan.subscriptions = {"{\"op\":\"subscribe\",\"args\":[" + Join(args) + "]}"}; plan.heartbeat = "ping";
    }
    else if (source == DataSource::Okx)
    {
        plan.host = L"ws.okx.com"; plan.port = 443; plan.path = L"/ws/v5/public";
        plan.subscriptions = {"{\"op\":\"subscribe\",\"args\":[" + Join(args) + "]}"}; plan.heartbeat = "ping";
    }
    else
    {
        plan.host = category == L"SPOT" ? L"stream.binance.com" : category == L"COIN-FUTURES" ? L"dstream.binance.com" : L"fstream.binance.com";
        plan.port = 443; plan.path = category == L"SPOT" ? L"/ws" : L"/market/ws";
        plan.subscriptions = {"{\"method\":\"SUBSCRIBE\",\"params\":[" + Join(args) + "],\"id\":1}"};
        plan.heartbeat = "{\"method\":\"LIST_SUBSCRIPTIONS\",\"id\":2}";
    }
    return plan;
}

std::vector<std::string> ApplyStreamMessage(const StreamPlan& plan, const std::string& message,
                                          std::map<std::string, Ticker>& cache)
{
    if (message == "pong" || message == "ping") return {};
    const Value root = Parser(message).Parse(); CheckError(root);
    std::vector<std::string> changed;
    const auto apply = [&](const Value& value, const std::string& wire, const std::string& channel, bool snapshot) {
        const auto found = plan.symbols.find(wire);
        if (found == plan.symbols.end()) return;
        auto& ticker = cache[found->second]; ticker.symbol = found->second;
        if (snapshot && (plan.source == DataSource::Bybit || plan.source == DataSource::Bitget))
        { ticker = Ticker{}; ticker.symbol = found->second; }
        if (plan.source == DataSource::Bybit || plan.source == DataSource::Bitget)
        {
            Patch(value, "lastPrice", ticker.lastPrice); Patch(value, "markPrice", ticker.markPrice);
            Patch(value, "indexPrice", ticker.indexPrice); Patch(value, "price24hPcnt", ticker.price24hPcnt);
        }
        else if (plan.source == DataSource::Kraken)
        {
            Patch(value, "last", ticker.lastPrice); Patch(value, "markPrice", ticker.markPrice); Patch(value, "index", ticker.indexPrice);
            // Kraken ticker change / change_pct are percentages, not fractions.
            if (plan.category != L"US-STOCK")
            {
                const char* field = plan.category == L"USD-FUTURES" ? "change" : "change_pct";
                if (value.object.count(field)) ticker.price24hPcnt = PercentFraction(value.Text(field));
            }
            Patch(value, "timestamp", ticker.timestamp);
            if (value.object.count("suspended") && value.Get("suspended").scalar == "true")
                throw std::runtime_error("Kraken market suspended");
        }
        else if (plan.source == DataSource::Okx)
        {
            if (channel == "mark-price") Patch(value, "markPx", ticker.markPrice);
            else if (channel == "index-tickers") Patch(value, "idxPx", ticker.indexPrice);
            else
            {
                Patch(value, "last", ticker.lastPrice);
                if (value.object.count("open24h")) ticker.price24hPcnt = Fraction(ticker.lastPrice, value.Text("open24h"));
            }
        }
        else
        {
            if (value.Text("e") == "markPriceUpdate")
            { Patch(value, "p", ticker.markPrice); Patch(value, "i", ticker.indexPrice); }
            else { Patch(value, "c", ticker.lastPrice); if (value.object.count("P")) ticker.price24hPcnt = PercentFraction(value.Text("P")); }
        }
        changed.push_back(found->second);
    };
    if (plan.source == DataSource::Bybit)
    {
        const auto topic = root.Text("topic");
        if (topic.rfind("tickers.", 0) == 0)
        {
            const auto found = plan.symbols.find(topic.substr(8));
            if (root.Text("type") == "delta" && (found == plan.symbols.end() || !cache.count(found->second))) return {};
            apply(root.Get("data"), topic.substr(8), "ticker", root.Text("type") == "snapshot");
        }
    }
    else if (plan.source == DataSource::Kraken)
    {
        if (root.Text("channel") == "ticker")
            for (const auto& value : root.Get("data").array) apply(value, value.Text("symbol"), "ticker", root.Text("type") == "snapshot");
        else if (root.Text("feed") == "ticker") apply(root, root.Text("product_id"), "ticker", true);
    }
    else if (plan.source == DataSource::Bitget || plan.source == DataSource::Okx)
    {
        if (root.object.count("data") && root.object.count("arg"))
        {
            const auto& arg = root.Get("arg");
            const std::string channel = plan.source == DataSource::Bitget ? arg.Text("topic") : arg.Text("channel");
            for (const auto& value : root.Get("data").array)
            {
                std::string wire = plan.source == DataSource::Bitget ? value.Text("symbol") : value.Text("instId");
                if (wire.empty()) wire = plan.source == DataSource::Bitget ? arg.Text("symbol") : arg.Text("instId");
                apply(value, wire, channel, root.Text("action") == "snapshot");
            }
        }
    }
    else
    {
        const Value& value = root.object.count("stream") ? root.Get("data") : root;
        if (!value.Text("s").empty()) apply(value, value.Text("s"), "ticker", false);
    }
    return changed;
}
} // namespace cryptoprice
