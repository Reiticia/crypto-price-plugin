#pragma once

#include "Config.h"
#include "Json.h"

#include <string>
#include <unordered_map>

namespace cryptoprice
{
class MarketClient
{
public:
    bool FetchCategory(DataSource source, const std::wstring& category, PriceType priceType,
                       std::unordered_map<std::string, Ticker>& tickers,
                       std::wstring& error) const;

    bool FetchStreamMetadata(DataSource source, const std::wstring& category,
                             std::vector<std::string>& pages, std::wstring& error) const;

private:
    bool FetchKraken(const std::wstring& category,
                     std::unordered_map<std::string, Ticker>& tickers, std::wstring& error) const;
    bool FetchBybit(const std::wstring& category,
                    std::unordered_map<std::string, Ticker>& tickers, std::wstring& error) const;
    bool FetchBitget(const std::wstring& category,
                     std::unordered_map<std::string, Ticker>& tickers,
                     std::wstring& error) const;
    bool FetchOkx(const std::wstring& category, PriceType priceType,
                  std::unordered_map<std::string, Ticker>& tickers,
                  std::wstring& error) const;
    bool FetchBinance(const std::wstring& category, PriceType priceType,
                      std::unordered_map<std::string, Ticker>& tickers,
                      std::wstring& error) const;
};

std::wstring Utf8ToWide(const std::string& value);
std::string WideToAscii(const std::wstring& value);
} // namespace cryptoprice
