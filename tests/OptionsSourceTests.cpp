#include "OptionsWindow.h"
#include "Module.h"

#include <Windows.h>
#include <iostream>
#include <string>

HINSTANCE g_moduleInstance{};
namespace
{
int failures{};
bool visited{};
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; ++failures; }
}
BOOL CALLBACK Inspect(HWND window, LPARAM)
{
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (std::wstring(name) != L"TrafficMonitorCryptoPriceOptions") return TRUE;
    visited = true;
    const HWND list = GetDlgItem(window, 1001), source = GetDlgItem(window, 1011);
    RECT sourceRect{}, symbolRect{}, priceRect{};
    GetWindowRect(source, &sourceRect);
    GetWindowRect(GetDlgItem(window, 1002), &symbolRect);
    GetWindowRect(GetDlgItem(window, 1006), &priceRect);
    Check(sourceRect.top > symbolRect.top && sourceRect.bottom < priceRect.top,
          "source control belongs to instrument section, above global price type");
    Check(SendMessageW(source, CB_GETCURSEL, 0, 0) == 2 && IsWindowEnabled(source),
          "first instrument loads Binance");
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1001, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list));
    Check(SendMessageW(source, CB_GETCURSEL, 0, 0) == 1 && IsWindowEnabled(source),
          "second instrument independently loads OKX");
    SendMessageW(list, LB_SETCURSEL, 2, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1001, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list));
    wchar_t text[128]{};
    GetWindowTextW(source, text, 128);
    Check(!IsWindowEnabled(source) && std::wstring(text) == L"Kraken xStocks",
          "stocks force Kraken and disable source selector");
    const HWND category = GetDlgItem(window, 1003);
    SendMessageW(category, CB_SETCURSEL, 0, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1003, CBN_SELCHANGE), reinterpret_cast<LPARAM>(category));
    Check(IsWindowEnabled(source) && SendMessageW(source, CB_GETCOUNT, 0, 0) == 3,
          "switching stock to crypto restores three source choices");
    SendMessageW(source, CB_SETCURSEL, 0, 0);
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1001, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list));
    Check(SendMessageW(source, CB_GETCURSEL, 0, 0) == 1,
          "uncommitted source editor does not change another instrument");
    PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
    return FALSE;
}
void CALLBACK Drive(HWND, UINT, UINT_PTR, DWORD)
{
    EnumThreadWindows(GetCurrentThreadId(), Inspect, 0);
}
}
int main()
{
    using namespace cryptoprice;
    g_moduleInstance = GetModuleHandleW(nullptr);
    Settings settings;
    settings.instruments = {{L"USDT-FUTURES", L"BTCUSDT", 2, DataSource::Binance},
                            {L"SPOT", L"ETHUSDT", 3, DataSource::Okx},
                            {L"US-STOCK", L"AAPL", 2, DataSource::Kraken}};
    const auto before = settings.instruments;
    const UINT_PTR timer = SetTimer(nullptr, 0, 50, Drive);
    if (!timer) return 2;
    const bool accepted = OptionsWindow::Show(nullptr, settings);
    KillTimer(nullptr, timer);
    Check(visited && !accepted, "dialog exercised and cancelled without network validation");
    Check(settings.instruments == before, "cancellation preserves independent sources");
    return failures ? 1 : 0;
}
