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
bool saveMode{};
bool submitted{};
cryptoprice::UpdateMode targetMode{cryptoprice::UpdateMode::WebSocket};
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
    if (saveMode)
    {
        if (submitted) return FALSE;
        submitted = true;
        const HWND mode = GetDlgItem(window, 1013);
        SendMessageW(mode, CB_SETCURSEL, targetMode == cryptoprice::UpdateMode::HttpPolling ? 1 : 0, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(1013, CBN_SELCHANGE), reinterpret_cast<LPARAM>(mode));
        SetWindowTextW(GetDlgItem(window, 1007), targetMode == cryptoprice::UpdateMode::HttpPolling ? L"17" : L"invalid");
        PostMessageW(window, WM_COMMAND, IDOK, 0);
        return FALSE;
    }
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
    Check(IsWindowEnabled(source) && SendMessageW(source, CB_GETCOUNT, 0, 0) == 5,
          "switching stock to crypto restores five source choices");
    SendMessageW(category, CB_SETCURSEL, 5, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1003, CBN_SELCHANGE), reinterpret_cast<LPARAM>(category));
    Check(SendMessageW(source, CB_GETCURSEL, 0, 0) == 3, "USD contracts default to Kraken");
    SendMessageW(source, CB_SETCURSEL, 4, 0);
    GetWindowTextW(source, text, 128);
    Check(std::wstring(text) == L"Bybit", "Bybit is selectable without shifting existing sources");
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1001, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list));
    Check(SendMessageW(source, CB_GETCURSEL, 0, 0) == 1,
          "uncommitted source editor does not change another instrument");
    const HWND mode = GetDlgItem(window, 1013), interval = GetDlgItem(window, 1007);
    Check(SendMessageW(mode, CB_GETCURSEL, 0, 0) == 0 && !IsWindowEnabled(interval),
          "WebSocket is default and ignores polling interval");
    SendMessageW(mode, CB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1013, CBN_SELCHANGE), reinterpret_cast<LPARAM>(mode));
    Check(IsWindowEnabled(interval), "HTTP mode enables polling interval");
    SetWindowTextW(interval, L"1");
    SendMessageW(window, WM_COMMAND, IDOK, 0);
    GetWindowTextW(GetDlgItem(window, 1009), text, 128);
    Check(IsWindow(window) && std::wstring(text).find(L"2 到 300") != std::wstring::npos,
          "HTTP mode rejects invalid interval before network validation");
    SendMessageW(mode, CB_SETCURSEL, 0, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1013, CBN_SELCHANGE), reinterpret_cast<LPARAM>(mode));
    Check(!IsWindowEnabled(interval), "switching back to WebSocket disables interval");
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
    Check(settings.updateMode == UpdateMode::WebSocket && settings.refreshSeconds == 5,
          "cancellation preserves transport settings");
    // Empty lists validate locally; exercise accept and transport persistence
    // without exchange connectivity or credentials.
    saveMode = true;
    settings.instruments.clear();
    targetMode = UpdateMode::HttpPolling;
    const UINT_PTR saveTimer = SetTimer(nullptr, 0, 50, Drive);
    if (!saveTimer) return 2;
    Check(OptionsWindow::Show(nullptr, settings), "accept HTTP transport settings");
    Check(settings.updateMode == UpdateMode::HttpPolling && settings.refreshSeconds == 17,
          "accepted HTTP mode preserves polling interval");
    submitted = false;
    targetMode = UpdateMode::WebSocket;
    Check(OptionsWindow::Show(nullptr, settings), "accept WebSocket with unused invalid interval text");
    Check(settings.updateMode == UpdateMode::WebSocket && settings.refreshSeconds == 17,
          "WebSocket ignores interval editor and retains previous HTTP interval");
    KillTimer(nullptr, saveTimer);
    return failures ? 1 : 0;
}
