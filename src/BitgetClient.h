#pragma once

#include "Json.h"

#include <string>
#include <unordered_map>

namespace bitget
{
class BitgetClient
{
public:
    bool FetchCategory(const std::wstring& category,
                       std::unordered_map<std::string, Ticker>& tickers,
                       std::wstring& error) const;
};

std::wstring Utf8ToWide(const std::string& value);
std::string WideToAscii(const std::wstring& value);
} // namespace bitget
