#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cryptoprice
{
constexpr std::size_t kMaxInstruments = 16;

enum class DataSource
{
    Bitget,
    Okx,
    Binance,
    Kraken,
    Bybit,
};

enum class UpdateMode
{
    WebSocket,
    HttpPolling,
};

enum class PriceType
{
    Last,
    Mark,
    Index,
};

struct Instrument
{
    std::wstring category{L"USDT-FUTURES"};
    std::wstring symbol{L"BTCUSDT"};
    // 该标的的价格小数位数：-1 表示自动（按行情价格字符串推断）；0..10 表示固定位数。
    int precision{-1};
    DataSource dataSource{DataSource::Bitget};

    bool operator==(const Instrument& other) const
    {
        return category == other.category && symbol == other.symbol &&
            (category == L"US-STOCK" || dataSource == other.dataSource);
    }
};

struct Settings
{
    std::vector<Instrument> instruments{{L"USDT-FUTURES", L"BTCUSDT"}};
    UpdateMode updateMode{UpdateMode::WebSocket};
    int refreshSeconds{5}; // HTTP polling only, 2..300 seconds.
    PriceType priceType{PriceType::Last};
    bool showChange{true};
};

// 按价格字符串推断小数位数（小数点后位数，限制在 0..10），供自动精度使用。
int InferPricePrecision(const std::string& raw);

std::wstring NormalizeSymbol(std::wstring value);
bool IsValidSymbol(const std::wstring& value);
std::wstring NormalizeStockSymbol(std::wstring value);
bool IsValidInstrumentSymbol(const Instrument& instrument);
bool IsSupportedMarket(DataSource source, const std::wstring& category);
bool Supports24HourChange(const Instrument& instrument);
DataSource InstrumentSource(const Instrument& instrument);
const wchar_t* InstrumentSourceName(const Instrument& instrument);
std::wstring FormatInstrumentSymbol(const Instrument& instrument);
const wchar_t* DataSourceKey(DataSource source);
const wchar_t* DataSourceName(DataSource source);
DataSource DataSourceFromKey(const std::wstring& value);
const wchar_t* PriceTypeKey(PriceType type);
PriceType PriceTypeFromKey(const std::wstring& value);

class ConfigStore
{
public:
    void SetDirectory(const std::wstring& directory);
    Settings Load() const;
    bool Save(const Settings& settings) const;
    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
};
} // namespace cryptoprice
