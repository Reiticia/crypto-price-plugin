#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace cryptoprice
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
bool ParseOkxTickerResponse(const std::string& json,
                            std::unordered_map<std::string, Ticker>& tickers,
                            std::string& error);
bool OverlayOkxPrices(const std::string& json, const std::string& field,
                      bool markPrice, std::unordered_map<std::string, Ticker>& tickers,
                      std::string& error);
bool ParseBinanceTickerResponse(const std::string& json, bool coinMargined,
                                std::unordered_map<std::string, Ticker>& tickers,
                                std::string& error);
bool OverlayBinancePremium(const std::string& json, bool coinMargined,
                           std::unordered_map<std::string, Ticker>& tickers,
                           std::string& error);
// Kraken xStocks: only canonical x/USD pairs; no 24h change is fabricated from today's open.
bool ParseKrakenStockResponse(const std::string& json,
                              std::unordered_map<std::string, Ticker>& tickers,
                              std::string& error);
bool ParseKrakenSpotResponse(const std::string& pairsJson, const std::string& tickerJson,
                            std::unordered_map<std::string, Ticker>& tickers, std::string& error);
bool ParseKrakenFuturesResponse(const std::string& json,
                               std::unordered_map<std::string, Ticker>& tickers, std::string& error);
bool ParseBybitResponse(const std::vector<std::string>& instruments, const std::string& json,
                        const std::wstring& category, std::unordered_map<std::string, Ticker>& tickers, std::string& error);
bool BybitNextCursor(const std::string& json, std::string& cursor, std::string& error);
std::vector<std::string> ParseDohIpv4Addresses(const std::string& json);
} // namespace cryptoprice
