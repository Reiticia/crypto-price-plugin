#include "Config.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

namespace cryptoprice
{
namespace
{
std::wstring ReadString(const std::wstring& path, const wchar_t* section,
                        const wchar_t* key, const wchar_t* fallback)
{
    wchar_t buffer[256]{};
    GetPrivateProfileStringW(section, key, fallback, buffer,
                             static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

// 读取小数位配置：缺省或脏值（非整数）一律回退 -1（自动推断）。
int ReadPrecision(const std::wstring& path, const wchar_t* section)
{
    const std::wstring text = ReadString(path, section, L"Precision", L"-1");
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    if (end != text.c_str() && *end == L'\0')
        return std::clamp(static_cast<int>(value), -1, 10);
    return -1;
}
}

int InferPricePrecision(const std::string& raw)
{
    const std::size_t point = raw.find('.');
    if (point == std::string::npos) return 0;
    std::size_t digits = 0;
    for (std::size_t index = point + 1; index < raw.size(); ++index)
    {
        const char character = raw[index];
        if (character < '0' || character > '9') break;
        ++digits;
    }
    return static_cast<int>((std::min)(digits, static_cast<std::size_t>(10)));
}

std::wstring NormalizeSymbol(std::wstring value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t character) {
        return std::iswspace(character) || character == L'/' || character == L'-' ||
               character == L'_';
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    return value;
}

bool IsValidSymbol(const std::wstring& value)
{
    if (value.size() < 3 || value.size() > 32)
        return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'A' && character <= L'Z') ||
               (character >= L'0' && character <= L'9');
    });
}

std::wstring NormalizeStockSymbol(std::wstring value)
{
    // Preserve a literal lowercase x before normalizing: NFLX/USD is a stock code,
    // while AAPLx/USD is Kraken's tokenized pair. Uppercasing first loses that distinction.
    value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t character) {
        return std::iswspace(character) || character == L'/' || character == L'-' || character == L'_';
    }), value.end());
    const bool tokenSuffix = value.size() > 4 && value[value.size() - 4] == L'x';
    value = NormalizeSymbol(std::move(value));
    if (value.size() > 3 && value.compare(value.size() - 3, 3, L"USD") == 0)
    {
        value.resize(value.size() - 3);
        if (tokenSuffix) value.pop_back();
    }
    return value;
}

bool IsValidInstrumentSymbol(const Instrument& instrument)
{
    if (instrument.category != L"US-STOCK") return IsValidSymbol(instrument.symbol);
    return !instrument.symbol.empty() && instrument.symbol.size() <= 16 &&
        std::all_of(instrument.symbol.begin(), instrument.symbol.end(), [](wchar_t character) {
            return character >= L'A' && character <= L'Z';
        });
}

DataSource InstrumentSource(const Instrument& instrument)
{
    return instrument.category == L"US-STOCK" ? DataSource::Kraken : instrument.dataSource;
}

const wchar_t* InstrumentSourceName(const Instrument& instrument)
{
    return DataSourceName(InstrumentSource(instrument));
}

std::wstring FormatInstrumentSymbol(const Instrument& instrument)
{
    if (instrument.category == L"US-STOCK") return instrument.symbol + L"/USD";
    if (instrument.category != L"SPOT") return instrument.symbol;

    static constexpr std::array<std::wstring_view, 12> quoteCurrencies{
        L"USDT", L"USDC", L"FDUSD", L"TUSD", L"DAI", L"BTC",
        L"ETH", L"BGB", L"EUR", L"BRL", L"TRY", L"GBP"
    };
    for (const std::wstring_view quote : quoteCurrencies)
    {
        if (instrument.symbol.size() <= quote.size()) continue;
        const std::size_t offset = instrument.symbol.size() - quote.size();
        if (instrument.symbol.compare(offset, quote.size(), quote) == 0)
            return instrument.symbol.substr(0, offset) + L"/" + std::wstring(quote);
    }
    return instrument.symbol;
}

const wchar_t* PriceTypeKey(PriceType type)
{
    switch (type)
    {
    case PriceType::Mark: return L"mark";
    case PriceType::Index: return L"index";
    default: return L"last";
    }
}

const wchar_t* DataSourceKey(DataSource source)
{
    switch (source)
    {
    case DataSource::Okx: return L"okx";
    case DataSource::Binance: return L"binance";
    case DataSource::Kraken: return L"kraken";
    default: return L"bitget";
    }
}

const wchar_t* DataSourceName(DataSource source)
{
    switch (source)
    {
    case DataSource::Okx: return L"OKX";
    case DataSource::Binance: return L"Binance";
    case DataSource::Kraken: return L"Kraken xStocks";
    default: return L"Bitget";
    }
}

DataSource DataSourceFromKey(const std::wstring& value)
{
    if (value == L"okx") return DataSource::Okx;
    if (value == L"binance") return DataSource::Binance;
    if (value == L"kraken") return DataSource::Kraken;
    return DataSource::Bitget;
}

PriceType PriceTypeFromKey(const std::wstring& value)
{
    if (value == L"mark") return PriceType::Mark;
    if (value == L"index") return PriceType::Index;
    return PriceType::Last;
}

void ConfigStore::SetDirectory(const std::wstring& directory)
{
    path_ = directory;
    if (!path_.empty() && path_.back() != L'\\' && path_.back() != L'/')
        path_.push_back(L'\\');
    path_ += L"CryptoPrice.ini";
}

Settings ConfigStore::Load() const
{
    Settings settings;
    if (path_.empty())
        return settings;
    const std::wstring* loadPath = &path_;
    if (GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES)
        return settings;

    settings.refreshSeconds = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"RefreshSeconds", 5, loadPath->c_str())),
        2, 300);
    settings.showChange = GetPrivateProfileIntW(L"General", L"ShowChange", 1, loadPath->c_str()) != 0;
    settings.priceType = PriceTypeFromKey(ReadString(*loadPath, L"General", L"PriceType", L"last"));
    // Migrate the old global source only when a per-instrument key is absent.
    const std::wstring legacySource = ReadString(*loadPath, L"General", L"DataSource", L"bitget");

    const int count = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"Count", 1, loadPath->c_str())),
        0, static_cast<int>(kMaxInstruments));
    settings.instruments.clear();
    for (int index = 0; index < count; ++index)
    {
        const std::wstring section = L"Instrument" + std::to_wstring(index);
        Instrument instrument;
        instrument.category = ReadString(*loadPath, section.c_str(), L"Category", L"USDT-FUTURES");
        const std::wstring symbol = ReadString(*loadPath, section.c_str(), L"Symbol", L"");
        instrument.symbol = instrument.category == L"US-STOCK" ? NormalizeStockSymbol(symbol) :
                            NormalizeSymbol(symbol);
        instrument.precision = ReadPrecision(*loadPath, section.c_str());
        instrument.dataSource = DataSourceFromKey(ReadString(*loadPath, section.c_str(),
            L"DataSource", legacySource.c_str()));
        if (instrument.category == L"US-STOCK") instrument.dataSource = DataSource::Kraken;
        else if (instrument.dataSource == DataSource::Kraken) instrument.dataSource = DataSource::Bitget;
        if ((instrument.category == L"SPOT" || instrument.category == L"USDT-FUTURES" ||
             instrument.category == L"USDC-FUTURES" || instrument.category == L"COIN-FUTURES" ||
             instrument.category == L"US-STOCK") && IsValidInstrumentSymbol(instrument))
            settings.instruments.push_back(std::move(instrument));
    }
    return settings;
}

bool ConfigStore::Save(const Settings& settings) const
{
    if (path_.empty()) return false;
    const auto write = [&](const wchar_t* section, const wchar_t* key, const std::wstring& value) {
        return WritePrivateProfileStringW(section, key, value.c_str(), path_.c_str()) != FALSE;
    };

    bool ok = true;
    // Remove the obsolete global key; each saved instrument carries its own source.
    ok &= write(L"General", L"RefreshSeconds", std::to_wstring(settings.refreshSeconds));
    ok &= write(L"General", L"ShowChange", settings.showChange ? L"1" : L"0");
    ok &= write(L"General", L"PriceType", PriceTypeKey(settings.priceType));
    const std::size_t count = (std::min)(settings.instruments.size(), kMaxInstruments);
    ok &= write(L"General", L"Count", std::to_wstring(count));
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::wstring section = L"Instrument" + std::to_wstring(index);
        ok &= write(section.c_str(), L"DataSource", DataSourceKey(InstrumentSource(settings.instruments[index])));
        ok &= write(section.c_str(), L"Category", settings.instruments[index].category);
        ok &= write(section.c_str(), L"Symbol", settings.instruments[index].symbol);
        ok &= write(section.c_str(), L"Precision",
                    std::to_wstring(std::clamp(settings.instruments[index].precision, -1, 10)));
    }
    if (ok) ok = WritePrivateProfileStringW(L"General", L"DataSource", nullptr, path_.c_str()) != FALSE;
    return ok;
}
} // namespace cryptoprice
