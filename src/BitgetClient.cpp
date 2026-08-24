#include "BitgetClient.h"

#include <Windows.h>
#include <winhttp.h>

#include <limits>
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

std::wstring WindowsError(const wchar_t* operation)
{
    return std::wstring(operation) + L"失败，Windows 错误 " + std::to_wstring(GetLastError());
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

bool BitgetClient::FetchCategory(const std::wstring& category,
                                 std::unordered_map<std::string, Ticker>& tickers,
                                 std::wstring& error) const
{
    InternetHandle session(WinHttpOpen(L"TrafficMonitor-BitgetPrice/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        error = WindowsError(L"初始化网络");
        return false;
    }
    WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 6000);
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(session.get(), WINHTTP_OPTION_DECOMPRESSION,
                     &decompression, sizeof(decompression));

    InternetHandle connection(WinHttpConnect(session.get(), L"api.bitget.com",
                                              INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection)
    {
        error = WindowsError(L"连接 Bitget");
        return false;
    }

    const std::wstring path = L"/api/v3/market/tickers?category=" + category;
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request)
    {
        error = WindowsError(L"创建请求");
        return false;
    }

    constexpr wchar_t headers[] = L"Accept: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
    {
        error = WindowsError(L"请求行情");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200)
    {
        error = L"Bitget HTTP 状态码 " + std::to_wstring(status);
        return false;
    }

    std::string body;
    constexpr std::size_t maximumBody = 16U * 1024U * 1024U;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            error = WindowsError(L"读取行情");
            return false;
        }
        if (available == 0) break;
        if (body.size() + available > maximumBody)
        {
            error = L"Bitget 响应过大";
            return false;
        }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), body.data() + offset, available, &bytesRead))
        {
            error = WindowsError(L"读取行情");
            return false;
        }
        body.resize(offset + bytesRead);
    }

    std::string parseError;
    if (!ParseTickerResponse(body, tickers, parseError))
    {
        error = Utf8ToWide(parseError);
        return false;
    }
    error.clear();
    return true;
}
} // namespace bitget
