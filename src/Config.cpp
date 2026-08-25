#include "Config.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

namespace bitget
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

std::wstring FormatInstrumentSymbol(const Instrument& instrument)
{
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
    default: return L"bitget";
    }
}

const wchar_t* DataSourceName(DataSource source)
{
    switch (source)
    {
    case DataSource::Okx: return L"OKX";
    case DataSource::Binance: return L"Binance";
    default: return L"Bitget";
    }
}

DataSource DataSourceFromKey(const std::wstring& value)
{
    if (value == L"okx") return DataSource::Okx;
    if (value == L"binance") return DataSource::Binance;
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
    legacyPath_ = path_ + L"BitgetPrice.ini";
    path_ += L"CryptoPrice.ini";
}

Settings ConfigStore::Load() const
{
    Settings settings;
    if (path_.empty())
        return settings;
    const std::wstring* loadPath = &path_;
    if (GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        if (GetFileAttributesW(legacyPath_.c_str()) == INVALID_FILE_ATTRIBUTES)
            return settings;
        loadPath = &legacyPath_;
    }

    settings.refreshSeconds = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"RefreshSeconds", 5, loadPath->c_str())),
        2, 300);
    settings.showChange = GetPrivateProfileIntW(L"General", L"ShowChange", 1, loadPath->c_str()) != 0;
    settings.priceType = PriceTypeFromKey(ReadString(*loadPath, L"General", L"PriceType", L"last"));
    settings.dataSource = DataSourceFromKey(ReadString(*loadPath, L"General", L"DataSource", L"bitget"));

    const int count = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"Count", 1, loadPath->c_str())),
        0, static_cast<int>(kMaxInstruments));
    settings.instruments.clear();
    for (int index = 0; index < count; ++index)
    {
        const std::wstring section = L"Instrument" + std::to_wstring(index);
        Instrument instrument;
        instrument.category = ReadString(*loadPath, section.c_str(), L"Category", L"USDT-FUTURES");
        instrument.symbol = NormalizeSymbol(ReadString(*loadPath, section.c_str(), L"Symbol", L""));
        if ((instrument.category == L"SPOT" || instrument.category == L"USDT-FUTURES" ||
             instrument.category == L"USDC-FUTURES" || instrument.category == L"COIN-FUTURES") &&
            IsValidSymbol(instrument.symbol))
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
    ok &= write(L"General", L"DataSource", DataSourceKey(settings.dataSource));
    ok &= write(L"General", L"RefreshSeconds", std::to_wstring(settings.refreshSeconds));
    ok &= write(L"General", L"ShowChange", settings.showChange ? L"1" : L"0");
    ok &= write(L"General", L"PriceType", PriceTypeKey(settings.priceType));
    const std::size_t count = (std::min)(settings.instruments.size(), kMaxInstruments);
    ok &= write(L"General", L"Count", std::to_wstring(count));
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::wstring section = L"Instrument" + std::to_wstring(index);
        ok &= write(section.c_str(), L"Category", settings.instruments[index].category);
        ok &= write(section.c_str(), L"Symbol", settings.instruments[index].symbol);
    }
    return ok;
}
} // namespace bitget
