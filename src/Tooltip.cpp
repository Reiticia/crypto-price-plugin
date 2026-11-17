#include "Tooltip.h"

namespace cryptoprice
{
std::wstring BuildCompactTooltip(const std::wstring& title,
                                const std::vector<std::wstring>& lines)
{
    constexpr wchar_t marker[] = L"\n…（更多见插件设置）";
    constexpr std::size_t markerLength = sizeof(marker) / sizeof(wchar_t) - 1;
    const std::size_t contentLimit = kMaxTooltipLength - markerLength;
    std::wstring result = title.substr(0, contentLimit);
    bool truncated = result.size() != title.size();
    for (const std::wstring& line : lines)
    {
        if (truncated || line.size() + 1 > contentLimit - result.size())
        {
            truncated = true;
            break;
        }
        result += L'\n';
        result += line;
    }
    if (truncated) result += marker;
    return result;
}
} // namespace cryptoprice
