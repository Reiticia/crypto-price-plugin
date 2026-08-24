#include "Json.h"

#include <cctype>
#include <string_view>

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
} // namespace bitget
