#include "CryptoPricePlugin.h"

#include "MarketClient.h"
#include "OptionsWindow.h"
#include "Tooltip.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace cryptoprice
{
namespace
{
std::wstring FormatPrice(const std::string& raw, int precision)
{
    if (raw.empty()) return L"--";
    char* end = nullptr;
    const double price = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0' || !std::isfinite(price)) return L"--";
    // 固定精度优先；未配置时按价格字符串自动推断小数位。
    const int effective = precision >= 0 ? precision : InferPricePrecision(raw);
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(effective) << price;
    return stream.str();
}

// 示例文本占位：固定精度时按位数生成，自动时用两位小数示意。
std::wstring PriceSample(int precision)
{
    if (precision < 0) return L"000000.00";
    std::wstring sample = L"000000";
    if (precision > 0)
    {
        sample += L'.';
        sample.append(static_cast<std::size_t>(precision), L'0');
    }
    return sample;
}

bool IsSpot(const Instrument& instrument)
{
    return instrument.category == L"SPOT" || instrument.category == L"US-STOCK";
}

std::wstring SummarySource(const Settings& settings)
{
    if (settings.instruments.empty()) return L"Crypto";
    const DataSource source = InstrumentSource(settings.instruments.front());
    for (const Instrument& instrument : settings.instruments)
        if (InstrumentSource(instrument) != source) return L"多源";
    return DataSourceName(source);
}

std::wstring DisplaySymbol(const Instrument& instrument)
{
    return FormatInstrumentSymbol(instrument);
}

std::wstring SelectPrice(const Ticker& ticker, PriceType type, bool spot, int precision)
{
    const std::string* value = &ticker.lastPrice;
    if (!spot && type == PriceType::Mark) value = &ticker.markPrice;
    else if (!spot && type == PriceType::Index) value = &ticker.indexPrice;
    return FormatPrice(*value, precision);
}

std::wstring FormatChange(const std::string& raw)
{
    if (raw.empty()) return L"";
    char* end = nullptr;
    const double percent = std::strtod(raw.c_str(), &end) * 100.0;
    if (end == raw.c_str()) return L"";
    std::wostringstream stream;
    stream << (percent >= 0.0 ? L" +" : L" ") << std::fixed << std::setprecision(2)
           << percent << L'%';
    return stream.str();
}
}

PriceItem::PriceItem(std::size_t index)
    : name_(L"Crypto 行情 " + std::to_wstring(index + 1)),
      id_(L"CryptoPrice" + std::to_wstring(index)),
      label_(L"Crypto: "), value_(L"--")
{
}

int PriceItem::GetItemWidthEx(void* hdc) const
{
    if (!customDraw_ || !hdc) return 0;
    const std::wstring sample = label_ + sample_;
    SIZE size{};
    return GetTextExtentPoint32W(static_cast<HDC>(hdc), sample.c_str(),
                                 static_cast<int>(sample.size()), &size)
        ? size.cx : 0;
}

void PriceItem::DrawItem(void* hdc, int x, int y, int width, int height, bool)
{
    if (!customDraw_ || !hdc || width <= 0 || height <= 0) return;

    HDC deviceContext = static_cast<HDC>(hdc);
    const int saved = SaveDC(deviceContext);
    SetBkMode(deviceContext, TRANSPARENT);

    const CryptoPricePlugin& plugin = CryptoPricePlugin::Instance();
    const COLORREF valueColor = plugin.drawingTaskbar_.load()
        ? static_cast<COLORREF>(plugin.valueTextColor_.load()) : GetTextColor(deviceContext);
    const COLORREF labelColor = plugin.drawingTaskbar_.load()
        ? static_cast<COLORREF>(plugin.labelTextColor_.load()) : valueColor;

    RECT labelRect{x, y, x + width, y + height};
    SIZE labelSize{};
    GetTextExtentPoint32W(deviceContext, label_.c_str(), static_cast<int>(label_.size()),
                          &labelSize);
    labelRect.right = (std::min)(labelRect.right, labelRect.left + labelSize.cx);
    SetTextColor(deviceContext, labelColor);
    DrawTextW(deviceContext, label_.c_str(), static_cast<int>(label_.size()), &labelRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const LONG valueLeft = (std::min)(static_cast<LONG>(x) + labelSize.cx,
                                      static_cast<LONG>(x + width));
    RECT valueRect{valueLeft, y, x + width, y + height};
    SetTextColor(deviceContext, valueColor);
    DrawTextW(deviceContext, value_.c_str(), static_cast<int>(value_.size()), &valueRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (saved != 0) RestoreDC(deviceContext, saved);
}

CryptoPricePlugin& CryptoPricePlugin::Instance()
{
    // Persistent WebSocket workers must never be joined under the DLL loader
    // lock. The module is pinned on first stream start; process exit reclaims it.
    static CryptoPricePlugin* instance = new CryptoPricePlugin();
    return *instance;
}

CryptoPricePlugin::CryptoPricePlugin()
    : items_{PriceItem(0), PriceItem(1), PriceItem(2), PriceItem(3),
             PriceItem(4), PriceItem(5), PriceItem(6), PriceItem(7),
             PriceItem(8), PriceItem(9), PriceItem(10), PriceItem(11),
             PriceItem(12), PriceItem(13), PriceItem(14), PriceItem(15),
             PriceItem(16)}
{
    // 保留旧版第一个显示项的 ID，使升级后原来已勾选的任务栏项目自动变为汇总项。
    items_[0].id_ = L"CryptoPrice0";
    for (std::size_t index = 0; index < kMaxInstruments; ++index)
    {
        items_[index + 1].id_ = L"CryptoPriceItem" + std::to_wstring(index);
        // TrafficMonitor 会按 ItemId 缓存标签。独立项的 symbol 会变化，因此由插件
        // 自绘标签和值，避免新标的继承同一槽位的旧标签。
        items_[index + 1].customDraw_ = true;
    }
    UpdatePlaceholder();
}

CryptoPricePlugin::~CryptoPricePlugin()
{
    shuttingDown_ = true;
    streams_.Stop();
}

IPluginItem* CryptoPricePlugin::GetItem(int index)
{
    if (index < 0) return nullptr;
    std::lock_guard<std::mutex> lock(settingsMutex_);
    // 未配置的槽位不注册，避免“显示设置”中出现无意义的占位项目。
    const std::size_t count = settings_.instruments.empty()
        ? 1 : settings_.instruments.size() + 1;
    if (static_cast<std::size_t>(index) >= count ||
        static_cast<std::size_t>(index) >= items_.size()) return nullptr;
    return &items_[static_cast<std::size_t>(index)];
}

void CryptoPricePlugin::DataRequired()
{
    ApplyPendingResult();
    StartRefresh(false);
}

ITMPlugin::OptionReturn CryptoPricePlugin::ShowOptionsDialog(void* parent)
{
    Settings draft;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        draft = settings_;
    }
    if (!OptionsWindow::Show(static_cast<HWND>(parent), draft))
        return OR_OPTION_UNCHANGED;

    draft.refreshSeconds = std::clamp(draft.refreshSeconds, 2, 300);
    if (!config_.Save(draft))
        MessageBoxW(static_cast<HWND>(parent), L"无法保存 CryptoPrice.ini，请检查配置目录权限。",
                    L"Crypto 行情", MB_OK | MB_ICONWARNING);
    ApplySettings(std::move(draft));
    StartRefresh(true);
    return OR_OPTION_CHANGED;
}

const wchar_t* CryptoPricePlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME: return L"Crypto 行情";
    case TMI_DESCRIPTION: return L"按标的选择 Bitget、OKX、Binance、Kraken、Bybit，监控现货、合约及美股行情";
    case TMI_AUTHOR: return L"Reiticia";
    case TMI_COPYRIGHT: return L"Copyright (C) 2026";
    case TMI_VERSION: return L"2.2.0";
    case TMI_URL: return L"https://github.com/zhongyang219/TrafficMonitorPlugins";
    default: return L"";
    }
}

const wchar_t* CryptoPricePlugin::GetTooltipInfo()
{
    return tooltip_.c_str();
}

void CryptoPricePlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (!data) return;
    switch (index)
    {
    case EI_CONFIG_DIR:
        config_.SetDirectory(data);
        ApplySettings(config_.Load());
        // Connect lazily on DataRequired; ABI enumeration does not access the network.
        break;
    case EI_DRAW_TASKBAR_WND:
        drawingTaskbar_ = std::wcstol(data, nullptr, 10) != 0;
        break;
    case EI_LABEL_TEXT_COLOR:
        labelTextColor_ = std::wcstoul(data, nullptr, 10);
        break;
    case EI_VALUE_TEXT_COLOR:
        valueTextColor_ = std::wcstoul(data, nullptr, 10);
        break;
    default:
        break;
    }
}

void CryptoPricePlugin::OnInitialize(ITrafficMonitor* application)
{
    if (!application) return;
    const wchar_t* directory = application->GetPluginConfigDir();
    if (directory && *directory)
    {
        config_.SetDirectory(directory);
        ApplySettings(config_.Load());
    }
}

const wchar_t* CryptoPricePlugin::GetCommandName(int commandIndex)
{
    return commandIndex == 0 ? L"重新连接行情推送" : nullptr;
}

void CryptoPricePlugin::OnPluginCommand(int commandIndex, void*, void*)
{
    if (commandIndex == 0) StartRefresh(true);
}

void CryptoPricePlugin::ApplySettings(Settings settings)
{
    if (settings.instruments.size() > kMaxInstruments)
        settings.instruments.resize(kMaxInstruments);
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        settings_ = std::move(settings);
        items_[0].name_ = SummarySource(settings_) + L" 行情汇总";
        items_[0].label_ = L"行情: ";
        for (std::size_t index = 0; index < settings_.instruments.size(); ++index)
        {
            PriceItem& item = items_[index + 1];
            item.name_ = std::wstring(InstrumentSourceName(settings_.instruments[index])) +
                         L" " + DisplaySymbol(settings_.instruments[index]);
            item.label_ = DisplaySymbol(settings_.instruments[index]) + L": ";
            item.value_ = L"--";
            item.sample_ = PriceSample(settings_.instruments[index].precision);
            if (settings_.showChange && Supports24HourChange(settings_.instruments[index]))
                item.sample_ += L" +00.00%";
            item.detail_ = L"等待行情";
        }
        // 已注册的空闲槽位也要清理，避免删除标的后继续显示原来的名称和价格。
        for (std::size_t index = settings_.instruments.size(); index < kMaxInstruments; ++index)
        {
            PriceItem& item = items_[index + 1];
            item.name_ = L"Crypto 行情（未配置槽位 " + std::to_wstring(index + 1) + L"）";
            item.label_ = L"Crypto: ";
            item.value_ = L"未配置";
            item.sample_ = L"未配置";
            item.detail_ = L"请在插件选项中添加标的";
        }
        UpdatePlaceholder();
        RebuildAggregate();
        RebuildTooltip(settings_.instruments.size());
    }
    ++generation_;
    {
        std::lock_guard<std::mutex> refreshLock(refreshMutex_);
        nextRefresh_ = std::chrono::steady_clock::time_point{};
        streamStarted_ = false;
    }
    streams_.Stop();
}

void CryptoPricePlugin::ApplyPendingResult()
{
    std::lock_guard<std::mutex> refreshLock(refreshMutex_);
    const auto now = std::chrono::steady_clock::now();
    const bool fullRefresh = now >= nextRefresh_;
    PendingResult result;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (!hasPending_) return;
        // Only a new/changed error bypasses the interval, and only for its own
        // item. Keep successful quotes pending until the original deadline.
        const bool urgentError = std::any_of(pending_.quotes.begin(), pending_.quotes.end(),
            [](const QuoteResult& quote) { return quote.urgentError; });
        if (!fullRefresh && !urgentError) return;
        result = pending_;
        for (auto& quote : pending_.quotes) quote.urgentError = false;
        if (fullRefresh) hasPending_ = false;
    }
    if (result.generation != generation_.load()) return;

    std::lock_guard<std::mutex> lock(settingsMutex_);
    if (fullRefresh) nextRefresh_ = now + std::chrono::seconds(settings_.refreshSeconds);
    const std::size_t count = (std::min)(result.quotes.size(), settings_.instruments.size());
    for (std::size_t index = 0; index < count; ++index)
        if (fullRefresh || result.quotes[index].urgentError)
            UpdateItem(index, result.quotes[index], settings_);
    RebuildAggregate();
    RebuildTooltip(settings_.instruments.size());
}

void CryptoPricePlugin::QueueStreamQuotes(unsigned long long generation, const std::vector<StreamQuote>& updates)
{
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (generation != generation_.load() || pending_.generation != generation) return;
    for (const auto& update : updates)
    {
        if (update.index >= pending_.quotes.size()) continue;
        auto& quote = pending_.quotes[update.index];
        quote.urgentError = !update.success &&
            (quote.urgentError || quote.success || quote.error != update.error);
        quote.success = update.success;
        quote.ticker = update.ticker;
        quote.error = update.error;
    }
    hasPending_ = true;
}

void CryptoPricePlugin::StartRefresh(bool force)
{
    std::lock_guard<std::mutex> refreshLock(refreshMutex_);
    if (shuttingDown_ || (streamStarted_ && !force)) return;
    Settings snapshot;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        snapshot = settings_;
    }
    const auto generation = generation_.load();
    nextRefresh_ = std::chrono::steady_clock::time_point{};
    streams_.Stop();
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_ = {generation, {}};
        for (const auto& instrument : snapshot.instruments)
            pending_.quotes.push_back({instrument, false, {}, L"等待 WebSocket 行情"});
        hasPending_ = false;
    }
    try
    {
        streams_.Start(snapshot, generation, [this](unsigned long long g, const std::vector<StreamQuote>& updates) {
            QueueStreamQuotes(g, updates);
        });
        streamStarted_ = true;
    }
    catch (...)
    {
        streamStarted_ = false;
    }
}

void CryptoPricePlugin::UpdatePlaceholder()
{
    if (!settings_.instruments.empty()) return;
    items_[0].name_ = L"Crypto 行情（未配置）";
    items_[0].label_ = L"Crypto: ";
    items_[0].value_ = L"未配置";
    items_[0].sample_ = L"未配置";
    items_[0].detail_ = L"请在插件管理中打开“选项”添加标的";
}

void CryptoPricePlugin::UpdateItem(std::size_t index, const QuoteResult& result,
                                   const Settings& settings)
{
    PriceItem& item = items_[index + 1];
    item.name_ = std::wstring(InstrumentSourceName(result.instrument)) + L" " +
                 DisplaySymbol(result.instrument);
    item.label_ = DisplaySymbol(result.instrument) + L": ";
    if (!result.success)
    {
        item.value_ = L"--";
        item.detail_ = result.error.empty() ? L"行情获取失败" : result.error;
        return;
    }
    item.value_ = SelectPrice(result.ticker, settings.priceType,
                              IsSpot(result.instrument), result.instrument.precision);
    if (item.value_ == L"--")
    {
        item.detail_ = L"WebSocket 已连接，等待所选价格频道快照";
        return;
    }
    if (settings.showChange && Supports24HourChange(result.instrument))
        item.value_ += FormatChange(result.ticker.price24hPcnt);
    item.detail_ = L"最新 " + FormatPrice(result.ticker.lastPrice, result.instrument.precision);
    if (result.instrument.category == L"US-STOCK")
        item.detail_ += L" USD | Kraken xStocks 代币化美股 | 不提供 24 小时涨跌幅";
    if (!IsSpot(result.instrument))
        item.detail_ += L" | 标记 " + FormatPrice(result.ticker.markPrice, result.instrument.precision) +
                        L" | 指数 " + FormatPrice(result.ticker.indexPrice, result.instrument.precision);
}

void CryptoPricePlugin::RebuildAggregate()
{
    if (settings_.instruments.empty()) return;

    PriceItem& aggregate = items_[0];
    aggregate.name_ = SummarySource(settings_) + L" 行情汇总";
    aggregate.label_ = L"行情: ";
    aggregate.value_.clear();
    aggregate.sample_.clear();
    aggregate.detail_ = L"共 " + std::to_wstring(settings_.instruments.size()) + L" 个标的";
    for (std::size_t index = 0; index < settings_.instruments.size(); ++index)
    {
        if (index != 0)
        {
            aggregate.value_ += L" | ";
            aggregate.sample_ += L" | ";
        }
        aggregate.value_ += DisplaySymbol(settings_.instruments[index]) + L" " + items_[index + 1].value_;
        aggregate.sample_ += DisplaySymbol(settings_.instruments[index]) + L" " +
                             PriceSample(settings_.instruments[index].precision);
        if (settings_.showChange && Supports24HourChange(settings_.instruments[index]))
            aggregate.sample_ += L" +00.00%";
    }
}

void CryptoPricePlugin::RebuildTooltip(std::size_t itemCount)
{
    std::wstring title = SummarySource(settings_) + L" 行情";
    const bool stocks = std::any_of(settings_.instruments.begin(), settings_.instruments.end(),
        [](const Instrument& instrument) { return instrument.category == L"US-STOCK"; });
    if (stocks) title += L"（美股 USD / 代币化 / 仅最新价）";
    std::vector<std::wstring> lines;
    if (itemCount == 0) lines.push_back(items_[0].detail_);
    for (std::size_t index = 0; index < itemCount && index < kMaxInstruments; ++index)
    {
        const PriceItem& item = items_[index + 1];
        // The value already contains the selected price; don't repeat last/mark/index
        // or the same stock disclaimer on every line. Bound errors independently.
        std::wstring line = std::wstring(InstrumentSourceName(settings_.instruments[index])) +
                            L" / " + item.label_ + item.value_;
        if (item.value_ == L"--" && !item.detail_.empty())
            line += L"（" + item.detail_.substr(0, 40) + (item.detail_.size() > 40 ? L"…" : L"") + L"）";
        lines.push_back(std::move(line));
    }
    tooltip_ = BuildCompactTooltip(title, lines);
}
} // namespace cryptoprice

ITMPlugin* TMPluginGetInstance()
{
    return &cryptoprice::CryptoPricePlugin::Instance();
}
