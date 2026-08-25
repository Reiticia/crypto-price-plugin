#include "MarketClient.h"

#include <Windows.h>
#include <winhttp.h>

#include <memory>

namespace bitget
{
namespace
{
struct InternetCloser
{
    void operator()(void* handle) const
    {
        if (handle) WinHttpCloseHandle(handle);
    }
};
using InternetHandle = std::unique_ptr<void, InternetCloser>;

std::wstring WindowsError(const std::wstring& provider, const wchar_t* operation)
{
    return provider + L" " + operation + L"失败，Windows 错误 " + std::to_wstring(GetLastError());
}

bool HttpGet(const wchar_t* host, const std::wstring& path, const std::wstring& provider,
             std::string& body, std::wstring& error)
{
    InternetHandle session(WinHttpOpen(L"TrafficMonitor-CryptoPrice/2.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        error = WindowsError(provider, L"初始化网络");
        return false;
    }
    WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 6000);
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(session.get(), WINHTTP_OPTION_DECOMPRESSION,
                     &decompression, sizeof(decompression));

    InternetHandle connection(WinHttpConnect(session.get(), host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection)
    {
        error = WindowsError(provider, L"连接");
        return false;
    }
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request)
    {
        error = WindowsError(provider, L"创建请求");
        return false;
    }

    constexpr wchar_t headers[] = L"Accept: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
    {
        error = WindowsError(provider, L"请求行情");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200)
    {
        error = provider + L" HTTP 状态码 " + std::to_wstring(status);
        return false;
    }

    body.clear();
    constexpr std::size_t maximumBody = 32U * 1024U * 1024U;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            error = WindowsError(provider, L"读取行情");
            return false;
        }
        if (available == 0) break;
        if (body.size() + available > maximumBody)
        {
            error = provider + L" 响应过大";
            return false;
        }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), body.data() + offset, available, &bytesRead))
        {
            error = WindowsError(provider, L"读取行情");
            return false;
        }
        body.resize(offset + bytesRead);
    }
    return true;
}

bool IsSpotCategory(const std::wstring& category)
{
    return category == L"SPOT";
}
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return std::wstring(value.begin(), value.end());
    std::wstring output(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), output.data(), length);
    return output;
}

std::string WideToAscii(const std::wstring& value)
{
    std::string output;
    output.reserve(value.size());
    for (const wchar_t character : value)
        output.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
    return output;
}

bool MarketClient::FetchCategory(DataSource source, const std::wstring& category,
                                 PriceType priceType,
                                 std::unordered_map<std::string, Ticker>& tickers,
                                 std::wstring& error) const
{
    switch (source)
    {
    case DataSource::Okx: return FetchOkx(category, priceType, tickers, error);
    case DataSource::Binance: return FetchBinance(category, priceType, tickers, error);
    default: return FetchBitget(category, tickers, error);
    }
}

bool MarketClient::FetchBitget(const std::wstring& category,
                               std::unordered_map<std::string, Ticker>& tickers,
                               std::wstring& error) const
{
    std::string body;
    if (!HttpGet(L"api.bitget.com", L"/api/v3/market/tickers?category=" + category,
                 L"Bitget", body, error))
        return false;
    std::string parseError;
    if (!ParseTickerResponse(body, tickers, parseError))
    {
        error = Utf8ToWide(parseError);
        return false;
    }
    error.clear();
    return true;
}

bool MarketClient::FetchOkx(const std::wstring& category, PriceType priceType,
                            std::unordered_map<std::string, Ticker>& tickers,
                            std::wstring& error) const
{
    const bool spot = IsSpotCategory(category);
    std::string body;
    const std::wstring tickerPath = L"/api/v5/market/tickers?instType=" +
                                    std::wstring(spot ? L"SPOT" : L"SWAP");
    if (!HttpGet(L"www.okx.com", tickerPath, L"OKX", body, error)) return false;
    std::string parseError;
    if (!ParseOkxTickerResponse(body, tickers, parseError))
    {
        error = Utf8ToWide(parseError);
        return false;
    }
    if (spot || priceType == PriceType::Last) return true;

    if (priceType == PriceType::Mark)
    {
        if (!HttpGet(L"www.okx.com", L"/api/v5/public/mark-price?instType=SWAP",
                     L"OKX", body, error)) return false;
        if (!OverlayOkxPrices(body, "markPx", true, tickers, parseError))
        {
            error = Utf8ToWide(parseError);
            return false;
        }
    }
    else
    {
        const wchar_t* quote = category == L"USDC-FUTURES" ? L"USDC" :
                               category == L"COIN-FUTURES" ? L"USD" : L"USDT";
        if (!HttpGet(L"www.okx.com", L"/api/v5/market/index-tickers?quoteCcy=" +
                     std::wstring(quote), L"OKX", body, error)) return false;
        if (!OverlayOkxPrices(body, "idxPx", false, tickers, parseError))
        {
            error = Utf8ToWide(parseError);
            return false;
        }
    }
    error.clear();
    return true;
}

bool MarketClient::FetchBinance(const std::wstring& category, PriceType priceType,
                                std::unordered_map<std::string, Ticker>& tickers,
                                std::wstring& error) const
{
    const bool spot = IsSpotCategory(category);
    const bool coin = category == L"COIN-FUTURES";
    const wchar_t* host = spot ? L"api.binance.com" :
                          coin ? L"dapi.binance.com" : L"fapi.binance.com";
    const wchar_t* tickerPath = spot ? L"/api/v3/ticker/24hr?type=MINI" :
                                coin ? L"/dapi/v1/ticker/24hr" : L"/fapi/v1/ticker/24hr";
    std::string body;
    if (!HttpGet(host, tickerPath, L"Binance", body, error)) return false;
    std::string parseError;
    if (!ParseBinanceTickerResponse(body, coin, tickers, parseError))
    {
        error = Utf8ToWide(parseError);
        return false;
    }
    if (spot || priceType == PriceType::Last) return true;

    const wchar_t* premiumPath = coin ? L"/dapi/v1/premiumIndex" : L"/fapi/v1/premiumIndex";
    if (!HttpGet(host, premiumPath, L"Binance", body, error)) return false;
    if (!OverlayBinancePremium(body, coin, tickers, parseError))
    {
        error = Utf8ToWide(parseError);
        return false;
    }
    error.clear();
    return true;
}
} // namespace bitget
