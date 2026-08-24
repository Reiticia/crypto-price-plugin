#include "BitgetPricePlugin.h"

#include "BitgetClient.h"
#include "OptionsWindow.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace bitget
{
namespace
{
std::wstring FormatPrice(const std::string& raw)
{
    if (raw.empty()) return L"--";
    char* end = nullptr;
    const double price = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0' || !std::isfinite(price)) return L"--";
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << price;
    return stream.str();
}

bool IsSpot(const Instrument& instrument)
{
    return instrument.category == L"SPOT";
}

std::wstring DisplaySymbol(const Instrument& instrument)
{
    return FormatInstrumentSymbol(instrument);
}

std::wstring SelectPrice(const Ticker& ticker, PriceType type, bool spot)
{
    const std::string* value = &ticker.lastPrice;
    if (!spot && type == PriceType::Mark) value = &ticker.markPrice;
    else if (!spot && type == PriceType::Index) value = &ticker.indexPrice;
    return FormatPrice(*value);
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
    : name_(L"Bitget 行情 " + std::to_wstring(index + 1)),
      id_(L"BitgetContractPrice" + std::to_wstring(index)),
      label_(L"Bitget: "), value_(L"--")
{
}

BitgetPricePlugin& BitgetPricePlugin::Instance()
{
    static BitgetPricePlugin instance;
    return instance;
}

BitgetPricePlugin::BitgetPricePlugin()
    : items_{PriceItem(0), PriceItem(1), PriceItem(2), PriceItem(3),
             PriceItem(4), PriceItem(5), PriceItem(6), PriceItem(7),
             PriceItem(8), PriceItem(9), PriceItem(10), PriceItem(11),
             PriceItem(12), PriceItem(13), PriceItem(14), PriceItem(15),
             PriceItem(16)}
{
    // 保留旧版第一个显示项的 ID，使升级后原来已勾选的任务栏项目自动变为汇总项。
    items_[0].id_ = L"BitgetContractPrice0";
    for (std::size_t index = 0; index < kMaxInstruments; ++index)
        items_[index + 1].id_ = L"BitgetContractPriceItem" + std::to_wstring(index);
    UpdatePlaceholder();
}

BitgetPricePlugin::~BitgetPricePlugin()
{
    shuttingDown_ = true;
    if (worker_.joinable()) worker_.join();
}

IPluginItem* BitgetPricePlugin::GetItem(int index)
{
    if (index < 0) return nullptr;
    std::lock_guard<std::mutex> lock(settingsMutex_);
    const std::size_t count = settings_.instruments.empty() ? 1 : settings_.instruments.size() + 1;
    if (static_cast<std::size_t>(index) >= count || static_cast<std::size_t>(index) >= items_.size())
        return nullptr;
    return &items_[static_cast<std::size_t>(index)];
}

void BitgetPricePlugin::DataRequired()
{
    ApplyPendingResult();
    StartRefresh(false);
}

ITMPlugin::OptionReturn BitgetPricePlugin::ShowOptionsDialog(void* parent)
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
        MessageBoxW(static_cast<HWND>(parent), L"无法保存 BitgetPrice.ini，请检查配置目录权限。",
                    L"Bitget 行情", MB_OK | MB_ICONWARNING);
    ApplySettings(std::move(draft));
    StartRefresh(true);
    return OR_OPTION_CHANGED;
}

const wchar_t* BitgetPricePlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME: return L"Bitget 行情";
    case TMI_DESCRIPTION: return L"同时监控多个 Bitget UTA 现货与合约行情";
    case TMI_AUTHOR: return L"OpenAI Codex";
    case TMI_COPYRIGHT: return L"Copyright (C) 2026";
    case TMI_VERSION: return L"1.2.0";
    case TMI_URL: return L"https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers";
    default: return L"";
    }
}

const wchar_t* BitgetPricePlugin::GetTooltipInfo()
{
    return tooltip_.c_str();
}

void BitgetPricePlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index != EI_CONFIG_DIR || !data) return;
    config_.SetDirectory(data);
    ApplySettings(config_.Load());
    StartRefresh(true);
}

void BitgetPricePlugin::OnInitialize(ITrafficMonitor* application)
{
    if (!application) return;
    const wchar_t* directory = application->GetPluginConfigDir();
    if (directory && *directory)
    {
        config_.SetDirectory(directory);
        ApplySettings(config_.Load());
    }
}

const wchar_t* BitgetPricePlugin::GetCommandName(int commandIndex)
{
    return commandIndex == 0 ? L"立即刷新 Bitget 行情" : nullptr;
}

void BitgetPricePlugin::OnPluginCommand(int commandIndex, void*, void*)
{
    if (commandIndex == 0) StartRefresh(true);
}

void BitgetPricePlugin::ApplySettings(Settings settings)
{
    if (settings.instruments.size() > kMaxInstruments)
        settings.instruments.resize(kMaxInstruments);
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        settings_ = std::move(settings);
        items_[0].name_ = L"Bitget 行情汇总";
        items_[0].label_ = L"Bitget: ";
        for (std::size_t index = 0; index < settings_.instruments.size(); ++index)
        {
            PriceItem& item = items_[index + 1];
            item.name_ = L"Bitget " + DisplaySymbol(settings_.instruments[index]);
            item.label_ = DisplaySymbol(settings_.instruments[index]) + L": ";
            item.value_ = L"--";
            item.detail_ = L"等待行情";
        }
        UpdatePlaceholder();
        RebuildAggregate();
        RebuildTooltip(settings_.instruments.size());
    }
    ++generation_;
    {
        std::lock_guard<std::mutex> refreshLock(refreshMutex_);
        nextRefresh_ = std::chrono::steady_clock::time_point{};
    }
}

void BitgetPricePlugin::ApplyPendingResult()
{
    PendingResult result;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (!hasPending_) return;
        result = std::move(pending_);
        hasPending_ = false;
    }
    if (result.generation != generation_.load()) return;

    std::lock_guard<std::mutex> lock(settingsMutex_);
    const std::size_t count = (std::min)(result.quotes.size(), settings_.instruments.size());
    for (std::size_t index = 0; index < count; ++index)
        UpdateItem(index, result.quotes[index], settings_);
    RebuildAggregate();
    RebuildTooltip(settings_.instruments.size());
}

void BitgetPricePlugin::StartRefresh(bool force)
{
    std::lock_guard<std::mutex> refreshLock(refreshMutex_);
    if (shuttingDown_) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < nextRefresh_) return;
    if (workerRunning_) return;
    if (worker_.joinable()) worker_.join();

    Settings snapshot;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        snapshot = settings_;
    }
    if (snapshot.instruments.empty()) return;
    nextRefresh_ = now + std::chrono::seconds(snapshot.refreshSeconds);
    const unsigned long long generation = generation_.load();
    workerRunning_ = true;
    worker_ = std::thread(&BitgetPricePlugin::RefreshWorker, this, std::move(snapshot), generation);
}

void BitgetPricePlugin::RefreshWorker(Settings settings, unsigned long long generation)
{
    std::map<std::wstring, std::vector<std::size_t>> groups;
    for (std::size_t index = 0; index < settings.instruments.size(); ++index)
        groups[settings.instruments[index].category].push_back(index);

    std::vector<QuoteResult> results(settings.instruments.size());
    BitgetClient client;
    for (const auto& [category, indexes] : groups)
    {
        if (shuttingDown_) break;
        std::unordered_map<std::string, Ticker> tickers;
        std::wstring requestError;
        const bool success = client.FetchCategory(category, tickers, requestError);
        for (const std::size_t index : indexes)
        {
            QuoteResult& result = results[index];
            result.instrument = settings.instruments[index];
            if (!success)
            {
                result.error = requestError;
                continue;
            }
            const auto ticker = tickers.find(WideToAscii(result.instrument.symbol));
            if (ticker == tickers.end())
            {
                result.error = L"未找到该标的，请检查市场类型和交易对";
                continue;
            }
            result.success = true;
            result.ticker = ticker->second;
        }
    }

    if (!shuttingDown_)
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_ = {generation, std::move(results)};
        hasPending_ = true;
    }
    workerRunning_ = false;
}

void BitgetPricePlugin::UpdatePlaceholder()
{
    if (!settings_.instruments.empty()) return;
    items_[0].name_ = L"Bitget 行情（未配置）";
    items_[0].label_ = L"Bitget: ";
    items_[0].value_ = L"未配置";
    items_[0].detail_ = L"请在插件管理中打开“选项”添加标的";
}

void BitgetPricePlugin::UpdateItem(std::size_t index, const QuoteResult& result,
                                   const Settings& settings)
{
    PriceItem& item = items_[index + 1];
    item.name_ = L"Bitget " + DisplaySymbol(result.instrument);
    item.label_ = DisplaySymbol(result.instrument) + L": ";
    if (!result.success)
    {
        item.value_ = L"--";
        item.detail_ = result.error.empty() ? L"行情获取失败" : result.error;
        return;
    }
    item.value_ = SelectPrice(result.ticker, settings.priceType, IsSpot(result.instrument));
    if (settings.showChange) item.value_ += FormatChange(result.ticker.price24hPcnt);
    item.detail_ = L"最新 " + FormatPrice(result.ticker.lastPrice);
    if (!IsSpot(result.instrument))
        item.detail_ += L" | 标记 " + FormatPrice(result.ticker.markPrice) +
                        L" | 指数 " + FormatPrice(result.ticker.indexPrice);
}

void BitgetPricePlugin::RebuildAggregate()
{
    if (settings_.instruments.empty()) return;

    PriceItem& aggregate = items_[0];
    aggregate.name_ = L"Bitget 行情汇总";
    aggregate.label_ = L"Bitget: ";
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
        aggregate.sample_ += DisplaySymbol(settings_.instruments[index]) + L" 000000.00";
        if (settings_.showChange) aggregate.sample_ += L" +00.00%";
    }
}

void BitgetPricePlugin::RebuildTooltip(std::size_t itemCount)
{
    tooltip_ = L"Bitget UTA 行情";
    if (itemCount == 0)
    {
        tooltip_ += L"\n" + items_[0].detail_;
        return;
    }
    for (std::size_t index = 0; index < itemCount && index < kMaxInstruments; ++index)
    {
        const PriceItem& item = items_[index + 1];
        tooltip_ += L"\n" + item.label_ + item.value_;
        if (!item.detail_.empty()) tooltip_ += L"（" + item.detail_ + L"）";
    }
}
} // namespace bitget

ITMPlugin* TMPluginGetInstance()
{
    return &bitget::BitgetPricePlugin::Instance();
}
