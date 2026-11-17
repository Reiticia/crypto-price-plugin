#include "InstrumentValidation.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <utility>

namespace cryptoprice
{
namespace
{
bool HasValidPrice(const Ticker& ticker, PriceType type, bool spot)
{
    const std::string& raw = spot || type == PriceType::Last ? ticker.lastPrice :
        type == PriceType::Mark ? ticker.markPrice : ticker.indexPrice;
    if (raw.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(raw.c_str(), &end);
    return end != raw.c_str() && end == raw.c_str() + raw.size() &&
        std::isfinite(value) && value > 0;
}

bool MatchesCategory(const Instrument& instrument)
{
    if (instrument.category == L"SPOT" || instrument.category == L"US-STOCK") return true;
    const std::wstring quote = instrument.category == L"USDT-FUTURES" ? L"USDT" :
        instrument.category == L"USDC-FUTURES" ? L"USDC" :
        (instrument.category == L"COIN-FUTURES" || instrument.category == L"USD-FUTURES") ? L"USD" : L"";
    return !quote.empty() && instrument.symbol.size() > quote.size() &&
        instrument.symbol.compare(instrument.symbol.size() - quote.size(), quote.size(), quote) == 0;
}
}

std::wstring ValidateInstruments(const Settings& settings, const CategoryFetcher& fetch)
{
    std::map<std::pair<DataSource, std::wstring>, std::unordered_map<std::string, Ticker>> categories;
    for (const Instrument& instrument : settings.instruments)
    {
        const std::wstring prefix = std::wstring(InstrumentSourceName(instrument)) + L" / " +
            instrument.category + L" / " + FormatInstrumentSymbol(instrument) + L"：";
        if (!IsSupportedMarket(InstrumentSource(instrument), instrument.category))
            return prefix + L"数据源不支持该市场；Kraken 加密行情请选择现货或 USD 本位合约。";
        if (!IsValidInstrumentSymbol(instrument) || !MatchesCategory(instrument))
            return prefix + L"交易对与市场类型不匹配，请检查后重试。";

        const auto key = std::make_pair(InstrumentSource(instrument), instrument.category);
        auto category = categories.find(key);
        if (category == categories.end())
        {
            std::unordered_map<std::string, Ticker> tickers;
            std::wstring error;
            if (!fetch(key.first, instrument.category, settings.priceType, tickers, error))
                return prefix + L"行情查询失败，无法验证，请稍后重试。" + error;
            category = categories.emplace(key, std::move(tickers)).first;
        }
        std::string symbol;
        symbol.reserve(instrument.symbol.size());
        for (const wchar_t character : instrument.symbol)
            symbol.push_back(static_cast<char>(character)); // IsValidSymbol 已确保为 ASCII。
        const auto ticker = category->second.find(symbol);
        if (ticker == category->second.end())
            return prefix + L"未找到该标的，请检查交易对、市场类型和数据源。";
        if (!HasValidPrice(ticker->second, settings.priceType,
                           instrument.category == L"SPOT" || instrument.category == L"US-STOCK"))
            return prefix + L"无法取得有效的所选价格，不允许添加或保存。";
    }
    return {};
}
} // namespace cryptoprice
