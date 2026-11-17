#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cryptoprice
{
// TrafficMonitor 1.85.1 concatenates all plugin tooltips into an MFC control
// limited to 1024 UTF-16 code units. Reserve room for the host and other plugins.
constexpr std::size_t kMaxTooltipLength = 384;

std::wstring BuildCompactTooltip(const std::wstring& title,
                                const std::vector<std::wstring>& lines);
} // namespace cryptoprice
