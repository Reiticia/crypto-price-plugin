#include "Json.h"

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
struct Value
{
    std::string scalar;
    std::map<std::string, Value> object;
    std::vector<Value> array;
    char kind{};
    const Value& Get(const std::string& key) const
    {
        const auto found = object.find(key);
        if (kind != '{' || found == object.end()) throw std::runtime_error("Missing field: " + key);
        return found->second;
    }
    std::string Text() const
    {
        if (kind != 's' && kind != 'n') return "";
        if (kind != 'n' || scalar.find_first_of("eE") == std::string::npos) return scalar;
        // Keep numeric Kraken prices compatible with decimal precision inference.
        const double value = std::strtod(scalar.c_str(), nullptr);
        if (!std::isfinite(value)) return "";
        std::ostringstream output;
        output << std::fixed << std::setprecision(10) << value;
        std::string result = output.str();
        while (result.size() > 1 && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
        return result;
    }
    std::string Text(const std::string& key) const
    {
        const auto found = object.find(key);
        return found == object.end() ? "" : found->second.Text();
    }
};
class Parser
{
public:
    explicit Parser(const std::string& text) : text_(text) {}
    Value Parse()
    {
        Value value = Read(0);
        Space();
        if (pos_ != text_.size()) throw std::runtime_error("Trailing JSON data");
        return value;
    }
private:
    void Space() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    char Take() { if (pos_ == text_.size()) throw std::runtime_error("Truncated JSON"); return text_[pos_++]; }
    void Expect(char ch) { Space(); if (Take() != ch) throw std::runtime_error("Invalid JSON delimiter"); }
    std::string String()
    {
        std::string result;
        while (true)
        {
            char ch = Take();
            if (ch == '"') return result;
            if (static_cast<unsigned char>(ch) < 32) throw std::runtime_error("Invalid JSON string");
            if (ch == '\\')
            {
                ch = Take();
                if (ch == 'u')
                {
                    for (int i = 0; i < 4; ++i)
                        if (!std::isxdigit(static_cast<unsigned char>(Take()))) throw std::runtime_error("Invalid Unicode escape");
                    result += '?'; // Exchange identifiers and price fields are ASCII.
                    continue;
                }
                if (ch == 'n') ch = '\n';
                else if (ch == 'r') ch = '\r';
                else if (ch == 't') ch = '\t';
                else if (ch == 'b') ch = '\b';
                else if (ch == 'f') ch = '\f';
                else if (ch != '"' && ch != '\\' && ch != '/') throw std::runtime_error("Invalid escape");
            }
            result += ch;
        }
    }
    Value Read(int depth)
    {
        if (depth > 32) throw std::runtime_error("JSON nesting too deep");
        Space();
        const char ch = Take();
        Value value;
        value.kind = ch;
        if (ch == '"') { value.kind = 's'; value.scalar = String(); return value; }
        if (ch == '{' || ch == '[')
        {
            const char end = ch == '{' ? '}' : ']';
            Space();
            if (pos_ < text_.size() && text_[pos_] == end) { ++pos_; return value; }
            while (true)
            {
                if (ch == '{')
                {
                    Expect('"');
                    const std::string key = String();
                    Expect(':');
                    if (!value.object.emplace(key, Read(depth + 1)).second) throw std::runtime_error("Duplicate JSON key");
                }
                else value.array.push_back(Read(depth + 1));
                Space();
                const char separator = Take();
                if (separator == end) return value;
                if (separator != ',') throw std::runtime_error("Invalid JSON separator");
            }
        }
        const std::size_t start = pos_ - 1;
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']' &&
               !std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        value.scalar = text_.substr(start, pos_ - start);
        if (value.scalar == "true" || value.scalar == "false" || value.scalar == "null")
        { value.kind = 'l'; return value; }
        // Enforce JSON number grammar, including exponent notation.
        std::size_t i = value.scalar[0] == '-' ? 1 : 0;
        const auto digits = [&] { const std::size_t begin = i; while (i < value.scalar.size() && std::isdigit(static_cast<unsigned char>(value.scalar[i]))) ++i; return i > begin; };
        if (i == value.scalar.size()) throw std::runtime_error("Invalid JSON number");
        if (value.scalar[i] == '0') ++i;
        else if (!digits()) throw std::runtime_error("Invalid JSON number");
        if (i < value.scalar.size() && value.scalar[i] == '.') { ++i; if (!digits()) throw std::runtime_error("Invalid fraction"); }
        if (i < value.scalar.size() && (value.scalar[i] == 'e' || value.scalar[i] == 'E'))
        { ++i; if (i < value.scalar.size() && (value.scalar[i] == '+' || value.scalar[i] == '-')) ++i; if (!digits()) throw std::runtime_error("Invalid exponent"); }
        if (i != value.scalar.size()) throw std::runtime_error("Invalid JSON scalar");
        value.kind = 'n';
        return value;
    }
    const std::string& text_;
    std::size_t pos_{};
};
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
