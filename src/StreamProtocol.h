#pragma once

#include "Config.h"
#include "Json.h"
#include <map>
#include <string>
#include <vector>

namespace cryptoprice
{
struct StreamPlan
{
    DataSource source{};
    std::wstring category;
    std::wstring host;
    unsigned short port{443};
    std::wstring path;
    std::vector<std::string> subscriptions;
    std::string heartbeat;
    // Wire identifier -> configured symbol (e.g. BTCPERP -> BTCUSDC).
    std::map<std::string, std::string> symbols;
};

std::string JsonQuote(const std::string& text);
// Metadata is optional except Kraken spot/stocks and Bybit. It contains no prices.
StreamPlan BuildStreamPlan(DataSource source, const std::wstring& category,
                           const std::vector<Instrument>& instruments,
                           const std::vector<std::string>& metadata);
// Connection-local cache. Snapshot resets only its own channel fields; deltas
// preserve omitted fields. Throws on protocol / subscription errors.
std::vector<std::string> ApplyStreamMessage(const StreamPlan& plan, const std::string& message,
                                          std::map<std::string, Ticker>& cache);
} // namespace cryptoprice
