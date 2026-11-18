#pragma once

#include "Config.h"
#include "Json.h"
#include "PluginInterface.h"
#include "StreamClient.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cryptoprice
{
class CryptoPricePlugin;

class PriceItem final : public IPluginItem
{
public:
    explicit PriceItem(std::size_t index = 0);

    const wchar_t* GetItemName() const override { return name_.c_str(); }
    const wchar_t* GetItemId() const override { return id_.c_str(); }
    const wchar_t* GetItemLableText() const override { return label_.c_str(); }
    const wchar_t* GetItemValueText() const override { return value_.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return sample_.c_str(); }
    bool IsCustomDraw() const override { return customDraw_; }
    int GetItemWidth() const override { return customDraw_ ? 180 : 0; }
    int GetItemWidthEx(void* hdc) const override;
    void DrawItem(void* hdc, int x, int y, int width, int height, bool) override;

private:
    friend class CryptoPricePlugin;
    std::wstring name_;
    std::wstring id_;
    std::wstring label_;
    std::wstring value_;
    std::wstring sample_{L"000000.00 +00.00%"};
    std::wstring detail_;
    bool customDraw_{};
};

class CryptoPricePlugin final : public ITMPlugin
{
public:
    static CryptoPricePlugin& Instance();
    ~CryptoPricePlugin();
    void Shutdown() noexcept { streams_.Shutdown(); }

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* parent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    void OnInitialize(ITrafficMonitor* application) override;
    int GetCommandCount() override { return 1; }
    const wchar_t* GetCommandName(int commandIndex) override;
    void OnPluginCommand(int commandIndex, void*, void*) override;

private:
    friend class PriceItem;
    friend struct CryptoPricePluginTestAccess;
    struct QuoteResult
    {
        Instrument instrument;
        bool success{};
        Ticker ticker;
        std::wstring error;
    };
    struct PendingResult
    {
        unsigned long long generation{};
        std::vector<QuoteResult> quotes;
    };

    CryptoPricePlugin();
    CryptoPricePlugin(const CryptoPricePlugin&) = delete;
    CryptoPricePlugin& operator=(const CryptoPricePlugin&) = delete;

    void ApplySettings(Settings settings);
    void ApplyPendingResult();
    void QueueStreamQuotes(unsigned long long generation, const std::vector<StreamQuote>& updates);
    void StartRefresh(bool force);
    void UpdatePlaceholder();
    void UpdateItem(std::size_t index, const QuoteResult& result, const Settings& settings);
    void RebuildAggregate();
    void RebuildTooltip(std::size_t itemCount);

    // 第 0 项是任务栏默认显示的汇总项，后续项目是每个标的的独立显示项。
    std::array<PriceItem, kMaxInstruments + 1> items_;
    Settings settings_;
    ConfigStore config_;
    mutable std::mutex settingsMutex_;

    std::mutex pendingMutex_;
    PendingResult pending_;
    bool hasPending_{};

    std::mutex refreshMutex_;
    StreamClient streams_;
    bool streamStarted_{};
    std::atomic<bool> shuttingDown_{false};
    std::atomic<unsigned long long> generation_{1};
    std::wstring tooltip_{L"Crypto 行情：等待数据"};
    std::atomic<bool> drawingTaskbar_{false};
    std::atomic<unsigned long> labelTextColor_{};
    std::atomic<unsigned long> valueTextColor_{};
};
} // namespace cryptoprice

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
