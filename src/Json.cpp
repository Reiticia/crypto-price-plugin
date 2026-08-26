#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <vector>

namespace bitget
{
namespace
{
bool ExtractString(std::string_view object, std::string_view key, std::string& output)
{
    const std::string token = "\"" + std::string(key) + "\"";
    std::size_t position = object.find(token);
    if (position == std::string_view::npos) return false;
    position = object.find(':', position + token.size());
    if (position == std::string_view::npos) return false;
    ++position;
    while (position < object.size() && std::isspace(static_cast<unsigned char>(object[position])))
        ++position;
    if (position >= object.size() || object[position] != '"') return false;
    ++position;

    output.clear();
    bool escaped = false;
    for (; position < object.size(); ++position)
    {
        const char character = object[position];
        if (escaped)
        {
            output.push_back(character);
            escaped = false;
        }
        else if (character == '\\')
        {
            escaped = true;
        }
        else if (character == '"')
        {
            return true;
        }
        else
        {
            output.push_back(character);
        }
    }
    return false;
}

bool ForEachArrayObject(const std::string& json, bool dataWrapped,
                        const std::function<bool(std::string_view)>& callback,
                        std::string& error)
{
    std::size_t arrayStart = std::string::npos;
    if (dataWrapped)
    {
        const std::size_t dataKey = json.find("\"data\"");
        if (dataKey != std::string::npos) arrayStart = json.find('[', dataKey);
    }
    else
    {
        arrayStart = json.find('[');
    }
    if (arrayStart == std::string::npos)
    {
        error = "Missing ticker array";
        return false;
    }

    std::size_t position = arrayStart + 1;
    while (position < json.size())
    {
        const std::size_t objectStart = json.find('{', position);
        const std::size_t arrayEnd = json.find(']', position);
        if (objectStart == std::string::npos || (arrayEnd != std::string::npos && arrayEnd < objectStart))
            break;

        bool inString = false;
        bool escaped = false;
        int depth = 0;
        std::size_t objectEnd = std::string::npos;
        for (std::size_t cursor = objectStart; cursor < json.size(); ++cursor)
        {
            const char character = json[cursor];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') inString = false;
            }
            else if (character == '"') inString = true;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0)
            {
                objectEnd = cursor;
                break;
            }
        }
        if (objectEnd == std::string::npos)
        {
            error = "Malformed ticker object";
            return false;
        }
        if (!callback(std::string_view(json.data() + objectStart, objectEnd - objectStart + 1)))
            return false;
        position = objectEnd + 1;
    }
    return true;
}

std::string NormalizeOkxSymbol(std::string value)
{
    constexpr std::string_view swapSuffix{"-SWAP"};
    if (value.size() > swapSuffix.size() &&
        value.compare(value.size() - swapSuffix.size(), swapSuffix.size(), swapSuffix) == 0)
        value.resize(value.size() - swapSuffix.size());
    std::string output;
    output.reserve(value.size());
    for (const char character : value)
        if (character != '-') output.push_back(character);
    return output;
}

std::string NormalizeBinanceSymbol(std::string value, bool coinMargined)
{
    if (!coinMargined) return value;
    constexpr std::string_view perpetualSuffix{"_PERP"};
    if (value.size() <= perpetualSuffix.size() ||
        value.compare(value.size() - perpetualSuffix.size(), perpetualSuffix.size(), perpetualSuffix) != 0)
        return {};
    value.resize(value.size() - perpetualSuffix.size());
    return value;
}

std::string FractionFromPercent(const std::string& percent)
{
    char* end = nullptr;
    const double value = std::strtod(percent.c_str(), &end);
    if (end == percent.c_str() || *end != '\0' || !std::isfinite(value)) return {};
    return std::to_string(value / 100.0);
}

std::string FractionFromOpen(const std::string& last, const std::string& open)
{
    char* lastEnd = nullptr;
    char* openEnd = nullptr;
    const double lastValue = std::strtod(last.c_str(), &lastEnd);
    const double openValue = std::strtod(open.c_str(), &openEnd);
    if (lastEnd == last.c_str() || *lastEnd != '\0' || openEnd == open.c_str() ||
        *openEnd != '\0' || !std::isfinite(lastValue) || !std::isfinite(openValue) || openValue == 0.0)
        return {};
    return std::to_string((lastValue - openValue) / openValue);
}

bool IsIpv4Literal(std::string_view value)
{
    int segmentCount = 0;
    std::size_t position = 0;
    while (position < value.size())
    {
        if (segmentCount == 4) return false;
        int number = 0;
        int digitCount = 0;
        while (position < value.size() && value[position] >= '0' && value[position] <= '9')
        {
            number = number * 10 + value[position] - '0';
            ++position;
            ++digitCount;
        }
        if (digitCount == 0 || digitCount > 3 || number > 255) return false;
        ++segmentCount;
        if (position == value.size()) break;
        if (value[position] != '.') return false;
        ++position;
    }
    return segmentCount == 4;
}
}

std::vector<std::string> ParseDohIpv4Addresses(const std::string& json)
{
    std::vector<std::string> addresses;
    std::size_t position = 0;
    while ((position = json.find("\"data\"", position)) != std::string::npos)
    {
        position = json.find(':', position + 6);
        if (position == std::string::npos) break;
        ++position;
        while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])))
            ++position;
        if (position >= json.size() || json[position] != '"') continue;
        const std::size_t end = json.find('"', ++position);
        if (end == std::string::npos) break;
        const std::string candidate = json.substr(position, end - position);
        if (IsIpv4Literal(candidate) &&
            std::find(addresses.begin(), addresses.end(), candidate) == addresses.end())
            addresses.push_back(candidate);
        position = end + 1;
    }
    return addresses;
}

bool ParseTickerResponse(const std::string& json,
                         std::unordered_map<std::string, Ticker>& tickers,
                         std::string& error)
{
    tickers.clear();
    std::string code;
    if (!ExtractString(json, "code", code))
    {
        error = "Missing response code";
        return false;
    }
    if (code != "00000")
    {
        std::string message;
        ExtractString(json, "msg", message);
        error = "Bitget " + code + (message.empty() ? std::string{} : ": " + message);
        return false;
    }

    const std::size_t dataKey = json.find("\"data\"");
    const std::size_t arrayStart = dataKey == std::string::npos ? std::string::npos : json.find('[', dataKey);
    if (arrayStart == std::string::npos)
    {
        error = "Missing ticker data";
        return false;
    }

    std::size_t position = arrayStart + 1;
    while (position < json.size())
    {
        const std::size_t objectStart = json.find('{', position);
        const std::size_t arrayEnd = json.find(']', position);
        if (objectStart == std::string::npos || (arrayEnd != std::string::npos && arrayEnd < objectStart))
            break;

        bool inString = false;
        bool escaped = false;
        int depth = 0;
        std::size_t objectEnd = std::string::npos;
        for (std::size_t cursor = objectStart; cursor < json.size(); ++cursor)
        {
            const char character = json[cursor];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') inString = false;
            }
            else if (character == '"') inString = true;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0)
            {
                objectEnd = cursor;
                break;
            }
        }
        if (objectEnd == std::string::npos)
        {
            error = "Malformed ticker object";
            return false;
        }

        const std::string_view object(json.data() + objectStart, objectEnd - objectStart + 1);
        Ticker ticker;
        if (ExtractString(object, "symbol", ticker.symbol) &&
            ExtractString(object, "lastPrice", ticker.lastPrice))
        {
            ExtractString(object, "markPrice", ticker.markPrice);
            ExtractString(object, "indexPrice", ticker.indexPrice);
            ExtractString(object, "price24hPcnt", ticker.price24hPcnt);
            ExtractString(object, "ts", ticker.timestamp);
            tickers[ticker.symbol] = std::move(ticker);
        }
        position = objectEnd + 1;
    }

    if (tickers.empty())
    {
        error = "No ticker returned";
        return false;
    }
    error.clear();
    return true;
}

bool ParseOkxTickerResponse(const std::string& json,
                            std::unordered_map<std::string, Ticker>& tickers,
                            std::string& error)
{
    tickers.clear();
    std::string code;
    if (!ExtractString(json, "code", code) || code != "0")
    {
        std::string message;
        ExtractString(json, "msg", message);
        error = "OKX " + (code.empty() ? std::string("response error") : code) +
                (message.empty() ? std::string{} : ": " + message);
        return false;
    }
    if (!ForEachArrayObject(json, true, [&](std::string_view object) {
        Ticker ticker;
        std::string instrumentId;
        std::string open;
        if (!ExtractString(object, "instId", instrumentId) ||
            !ExtractString(object, "last", ticker.lastPrice))
            return true;
        ticker.symbol = NormalizeOkxSymbol(std::move(instrumentId));
        ExtractString(object, "open24h", open);
        ExtractString(object, "ts", ticker.timestamp);
        ticker.price24hPcnt = FractionFromOpen(ticker.lastPrice, open);
        if (!ticker.symbol.empty()) tickers[ticker.symbol] = std::move(ticker);
        return true;
    }, error)) return false;
    if (tickers.empty())
    {
        error = "OKX returned no ticker";
        return false;
    }
    error.clear();
    return true;
}

bool OverlayOkxPrices(const std::string& json, const std::string& field,
                      bool markPrice, std::unordered_map<std::string, Ticker>& tickers,
                      std::string& error)
{
    std::string code;
    if (!ExtractString(json, "code", code) || code != "0")
    {
        std::string message;
        ExtractString(json, "msg", message);
        error = "OKX price overlay error" + (message.empty() ? std::string{} : ": " + message);
        return false;
    }
    if (!ForEachArrayObject(json, true, [&](std::string_view object) {
        std::string instrumentId;
        std::string price;
        if (!ExtractString(object, "instId", instrumentId) || !ExtractString(object, field, price))
            return true;
        const auto ticker = tickers.find(NormalizeOkxSymbol(std::move(instrumentId)));
        if (ticker != tickers.end())
        {
            if (markPrice) ticker->second.markPrice = std::move(price);
            else ticker->second.indexPrice = std::move(price);
        }
        return true;
    }, error)) return false;
    error.clear();
    return true;
}

bool ParseBinanceTickerResponse(const std::string& json, bool coinMargined,
                                std::unordered_map<std::string, Ticker>& tickers,
                                std::string& error)
{
    tickers.clear();
    if (!ForEachArrayObject(json, false, [&](std::string_view object) {
        Ticker ticker;
        std::string percent;
        if (!ExtractString(object, "symbol", ticker.symbol) ||
            !ExtractString(object, "lastPrice", ticker.lastPrice))
            return true;
        ticker.symbol = NormalizeBinanceSymbol(std::move(ticker.symbol), coinMargined);
        ExtractString(object, "priceChangePercent", percent);
        ticker.price24hPcnt = FractionFromPercent(percent);
        if (!ticker.symbol.empty()) tickers[ticker.symbol] = std::move(ticker);
        return true;
    }, error))
    {
        std::string message;
        ExtractString(json, "msg", message);
        if (!message.empty()) error = "Binance: " + message;
        return false;
    }
    if (tickers.empty())
    {
        error = "Binance returned no ticker";
        return false;
    }
    error.clear();
    return true;
}

bool OverlayBinancePremium(const std::string& json, bool coinMargined,
                           std::unordered_map<std::string, Ticker>& tickers,
                           std::string& error)
{
    if (!ForEachArrayObject(json, false, [&](std::string_view object) {
        std::string symbol;
        if (!ExtractString(object, "symbol", symbol)) return true;
        symbol = NormalizeBinanceSymbol(std::move(symbol), coinMargined);
        const auto ticker = tickers.find(symbol);
        if (ticker == tickers.end()) return true;
        ExtractString(object, "markPrice", ticker->second.markPrice);
        ExtractString(object, "indexPrice", ticker->second.indexPrice);
        return true;
    }, error))
    {
        std::string message;
        ExtractString(json, "msg", message);
        if (!message.empty()) error = "Binance: " + message;
        return false;
    }
    error.clear();
    return true;
}
} // namespace bitget
