#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bitget
{
constexpr std::size_t kMaxInstruments = 16;

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
    int refreshSeconds{5};
    PriceType priceType{PriceType::Last};
    bool showChange{true};
};

std::wstring NormalizeSymbol(std::wstring value);
bool IsValidSymbol(const std::wstring& value);
std::wstring FormatInstrumentSymbol(const Instrument& instrument);
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
} // namespace bitget
