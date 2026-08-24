#pragma once

#include <string>
#include <unordered_map>

namespace bitget
{
struct Ticker
{
    std::string symbol;
    std::string lastPrice;
    std::string markPrice;
    std::string indexPrice;
    std::string price24hPcnt;
    std::string timestamp;
};

bool ParseTickerResponse(const std::string& json,
                         std::unordered_map<std::string, Ticker>& tickers,
                         std::string& error);
} // namespace bitget
