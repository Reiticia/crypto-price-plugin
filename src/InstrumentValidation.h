#pragma once

#include "Config.h"
#include "Json.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace cryptoprice
{
using CategoryFetcher = std::function<bool(DataSource, const std::wstring&, PriceType,
    std::unordered_map<std::string, Ticker>&, std::wstring&)>;

// 同一市场只查询一次；现货始终校验最新价，合约校验所选价格字段。
// 空字符串表示所有标的通过，否则返回包含失败标的的提示。
std::wstring ValidateInstruments(const Settings& settings, const CategoryFetcher& fetch);
} // namespace cryptoprice
