#include "MarketClient.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cwchar>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace cryptoprice
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
    const DWORD code = GetLastError();
    if (code == ERROR_WINHTTP_NAME_NOT_RESOLVED)
        return provider + L" " + operation +
               L"失败：无法解析服务器域名（Windows 错误 12007）";
    return provider + L" " + operation + L"失败，Windows 错误 " + std::to_wstring(code);
}

bool HttpGet(const wchar_t* host, const std::wstring& path, const std::wstring& provider,
             std::string& body, std::wstring& error,
             const wchar_t* resolutionHost = nullptr,
             const wchar_t* accept = L"application/json",
             const wchar_t* proxy = nullptr)
{
    // 指定代理时直接以 NAMED_PROXY 打开，避免自动代理检测与后续设置冲突；
    // 无代理时走系统自动代理（含系统代理和 WPAD）。
    InternetHandle session(proxy && *proxy
        ? WinHttpOpen(L"TrafficMonitor-CryptoPrice/2.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                      proxy, WINHTTP_NO_PROXY_BYPASS, 0)
        : WinHttpOpen(L"TrafficMonitor-CryptoPrice/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
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

    if (resolutionHost && *resolutionHost &&
        !WinHttpSetOption(request.get(), WINHTTP_OPTION_RESOLUTION_HOSTNAME,
                          const_cast<wchar_t*>(resolutionHost),
                          static_cast<DWORD>((std::wcslen(resolutionHost) + 1) * sizeof(wchar_t))))
    {
        error = WindowsError(provider, L"设置 DNS 回退");
        return false;
    }

    const std::wstring headers = L"Accept: " + std::wstring(accept) + L"\r\n";
    if (!WinHttpSendRequest(request.get(), headers.c_str(), static_cast<DWORD>(-1L),
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

// 部分网络会将 www.okx.com 的 DNS 解析长时间挂起或返回错误地址；
// 区域公共入口 eea.okx.com 与主入口使用同一套 v5 API。
constexpr std::array<const wchar_t*, 2> kOkxHosts{L"eea.okx.com", L"www.okx.com"};
std::atomic_size_t preferredOkxHost{0};
std::mutex okxAddressMutex;
std::wstring preferredOkxAddress;

std::wstring SystemProxyServer()
{
    // WinHTTP 直接读取当前用户的系统代理配置（对应 IE/WinINet 设置），
    // 不需要额外链接 advapi32。
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config)) return {};
    std::wstring proxy;
    if (config.lpszProxy && config.lpszProxy[0]) proxy = config.lpszProxy;
    if (config.lpszAutoConfigUrl) GlobalFree(config.lpszAutoConfigUrl);
    if (config.lpszProxy) GlobalFree(config.lpszProxy);
    if (config.lpszProxyBypass) GlobalFree(config.lpszProxyBypass);
    return proxy;
}

// 系统代理之外再尝试常见的本地代理端口（Clash / v2rayN 等）。
// 用户通常在“系统代理关闭、代理软件单独运行”时也能通过这些端口访问 OKX。
std::vector<std::wstring> LocalProxyCandidates()
{
    std::vector<std::wstring> candidates;
    const std::wstring systemProxy = SystemProxyServer();
    if (!systemProxy.empty()) candidates.push_back(systemProxy);
    static constexpr const wchar_t* kLocalPorts[] = {
        L"127.0.0.1:7897", L"127.0.0.1:7890", L"127.0.0.1:10809", L"127.0.0.1:1080",
    };
    for (const wchar_t* port : kLocalPorts)
        if (std::find(candidates.begin(), candidates.end(), port) == candidates.end())
            candidates.push_back(port);
    return candidates;
}

struct DohResolver
{
    const wchar_t* host;       // DoH 服务器域名（TLS SNI）
    const wchar_t* address;    // 直连 IP，绕过本地被污染的 DNS
    const wchar_t* queryPrefix; // cloudflare 使用 /dns-query，dns.google 使用 /resolve
};

std::vector<std::string> ResolveOkxAddresses(std::wstring& error)
{
    static constexpr std::array<DohResolver, 2> kResolvers{{
        {L"cloudflare-dns.com", L"1.1.1.1", L"/dns-query?name="},
        {L"dns.google", L"8.8.8.8", L"/resolve?name="},
    }};
    std::vector<std::string> addresses;
    std::wstring failures;
    for (const DohResolver& resolver : kResolvers)
    {
        for (const wchar_t* host : kOkxHosts)
        {
            std::string body;
            std::wstring attemptError;
            const std::wstring query =
                std::wstring(resolver.queryPrefix) + host + L"&type=A";
            if (!HttpGet(resolver.host, query, L"OKX DNS 回退", body, attemptError,
                         resolver.address, L"application/dns-json"))
            {
                if (!failures.empty()) failures += L"；";
                failures += std::move(attemptError);
                continue;
            }
            for (std::string& address : ParseDohIpv4Addresses(body))
            {
                if (std::find(addresses.begin(), addresses.end(), address) == addresses.end())
                    addresses.push_back(std::move(address));
            }
        }
    }
    if (addresses.empty())
    {
        if (failures.empty()) failures = L"OKX DNS 回退没有返回有效 IPv4 地址";
        error = std::move(failures);
    }
    return addresses;
}

bool OkxHttpGet(const std::wstring& path, std::string& body, std::wstring& error)
{
    std::wstring cachedAddress;
    {
        std::lock_guard<std::mutex> lock(okxAddressMutex);
        cachedAddress = preferredOkxAddress;
    }
    if (!cachedAddress.empty())
    {
        std::wstring attemptError;
        if (HttpGet(kOkxHosts[0], path, L"OKX (DNS 回退)", body, attemptError,
                    cachedAddress.c_str()))
        {
            error.clear();
            return true;
        }
        std::lock_guard<std::mutex> lock(okxAddressMutex);
        preferredOkxAddress.clear();
    }

    const std::size_t preferred = preferredOkxHost.load() % kOkxHosts.size();
    std::wstring failures;
    for (std::size_t offset = 0; offset < kOkxHosts.size(); ++offset)
    {
        const std::size_t index = (preferred + offset) % kOkxHosts.size();
        const wchar_t* host = kOkxHosts[index];
        std::wstring attemptError;
        if (HttpGet(host, path, L"OKX (" + std::wstring(host) + L")", body, attemptError))
        {
            preferredOkxHost.store(index);
            error.clear();
            return true;
        }
        if (!failures.empty()) failures += L"；";
        failures += std::move(attemptError);
    }

    // 本地代理（Clash / v2rayN 等）：代理端自行解析域名，可绕过被污染的 DNS。
    for (const std::wstring& proxy : LocalProxyCandidates())
    {
        std::wstring attemptError;
        if (HttpGet(kOkxHosts[0], path, L"OKX (代理 " + proxy + L")", body, attemptError,
                    nullptr, L"application/json", proxy.c_str()))
        {
            preferredOkxHost.store(0);
            error.clear();
            return true;
        }
        if (!failures.empty()) failures += L"；";
        failures += std::move(attemptError);
    }

    // DoH 解析真实 IP 后直连，绕过被污染的 DNS（代理不可用时的最后手段）。
    std::wstring resolveError;
    const std::vector<std::string> addresses = ResolveOkxAddresses(resolveError);
    for (const std::string& address : addresses)
    {
        const std::wstring wideAddress(address.begin(), address.end());
        std::wstring attemptError;
        if (HttpGet(kOkxHosts[0], path, L"OKX (DNS 回退)", body, attemptError,
                    wideAddress.c_str()))
        {
            std::lock_guard<std::mutex> lock(okxAddressMutex);
            preferredOkxAddress = wideAddress;
            error.clear();
            return true;
        }
        if (!failures.empty()) failures += L"；";
        failures += std::move(attemptError);
    }
    if (!resolveError.empty())
    {
        if (!failures.empty()) failures += L"；";
        failures += std::move(resolveError);
    }
    error = std::move(failures);
    return false;
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
    if (!OkxHttpGet(tickerPath, body, error)) return false;
    std::string parseError;
    if (!ParseOkxTickerResponse(body, tickers, parseError))
    {
        error = Utf8ToWide(parseError);
        return false;
    }
    if (spot || priceType == PriceType::Last) return true;

    if (priceType == PriceType::Mark)
    {
        if (!OkxHttpGet(L"/api/v5/public/mark-price?instType=SWAP", body, error)) return false;
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
        if (!OkxHttpGet(L"/api/v5/market/index-tickers?quoteCcy=" + std::wstring(quote),
                        body, error)) return false;
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
} // namespace cryptoprice
