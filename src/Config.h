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

    bool operator==(const Instrument& other) const
    {
        return category == other.category && symbol == other.symbol;
    }
};

struct Settings
{
    std::vector<Instrument> instruments{{L"USDT-FUTURES", L"BTCUSDT"}};
    DataSource dataSource{DataSource::Bitget};
    int refreshSeconds{5};
    PriceType priceType{PriceType::Last};
    bool showChange{true};
    // 价格小数位数：-1 表示自动（按行情价格字符串推断）；0..10 表示固定位数。
    int precision{-1};
};

// 按价格字符串推断小数位数（小数点后位数，限制在 0..10），供自动精度使用。
int InferPricePrecision(const std::string& raw);

std::wstring NormalizeSymbol(std::wstring value);
bool IsValidSymbol(const std::wstring& value);
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
