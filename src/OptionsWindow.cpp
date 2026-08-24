#include "OptionsWindow.h"

#include "Module.h"

#include <CommCtrl.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>

namespace bitget
{
namespace
{
constexpr wchar_t kWindowClass[] = L"TrafficMonitorBitgetPriceOptions";
constexpr int kList = 1001;
constexpr int kSymbol = 1002;
constexpr int kCategory = 1003;
constexpr int kAdd = 1004;
constexpr int kRemove = 1005;
constexpr int kPriceType = 1006;
constexpr int kRefresh = 1007;
constexpr int kShowChange = 1008;
constexpr int kStatus = 1009;
constexpr int kUpdate = 1010;

HWND AddControl(HWND parent, const wchar_t* className, const wchar_t* text,
                DWORD style, int x, int y, int width, int height, int id)
{
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_moduleInstance, nullptr);
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

const wchar_t* CategoryCaption(const std::wstring& category)
{
    if (category == L"SPOT") return L"现货";
    if (category == L"USDC-FUTURES") return L"USDC 本位合约";
    if (category == L"COIN-FUTURES") return L"币本位合约";
    return L"USDT 本位合约";
}

int CategoryIndex(const std::wstring& category)
{
    if (category == L"USDC-FUTURES") return 1;
    if (category == L"COIN-FUTURES") return 2;
    if (category == L"SPOT") return 3;
    return 0;
}

std::wstring CategoryAt(int index)
{
    if (index == 1) return L"USDC-FUTURES";
    if (index == 2) return L"COIN-FUTURES";
    if (index == 3) return L"SPOT";
    return L"USDT-FUTURES";
}
}

bool OptionsWindow::Show(HWND parent, Settings& settings)
{
    OptionsWindow dialog(settings);
    if (!dialog.Create(parent))
    {
        MessageBoxW(parent, L"无法创建选项窗口。", L"Bitget 行情", MB_OK | MB_ICONERROR);
        return false;
    }

    const bool parentWasEnabled = parent && IsWindowEnabled(parent);
    if (parentWasEnabled) EnableWindow(parent, FALSE);
    ShowWindow(dialog.window_, SW_SHOW);
    UpdateWindow(dialog.window_);

    MSG message{};
    while (IsWindow(dialog.window_))
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(dialog.window_, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (parentWasEnabled)
    {
        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);
    }
    if (dialog.accepted_) settings = std::move(dialog.draft_);
    return dialog.accepted_;
}

bool OptionsWindow::Create(HWND parent)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = g_moduleInstance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    RECT rectangle{0, 0, 660, 440};
    AdjustWindowRectEx(&rectangle, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (parent)
    {
        RECT parentRect{};
        GetWindowRect(parent, &parentRect);
        x = parentRect.left + ((parentRect.right - parentRect.left) - (rectangle.right - rectangle.left)) / 2;
        y = parentRect.top + ((parentRect.bottom - parentRect.top) - (rectangle.bottom - rectangle.top)) / 2;
    }
    window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass, L"Bitget 行情设置",
        WS_CAPTION | WS_SYSMENU, x, y, rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top, parent, nullptr, g_moduleInstance, this);
    return window_ != nullptr;
}

LRESULT CALLBACK OptionsWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    OptionsWindow* self = reinterpret_cast<OptionsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OptionsWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OptionsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateControls();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kList:
            if (HIWORD(wParam) == LBN_SELCHANGE) LoadSelection();
            return 0;
        case kAdd: AddNew(); return 0;
        case kUpdate: UpdateSelected(); return 0;
        case kRemove: RemoveSelected(); return 0;
        case IDOK: Accept(); return 0;
        case IDCANCEL: DestroyWindow(window_); return 0;
        default: break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        window_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void OptionsWindow::CreateControls()
{
    AddControl(window_, L"STATIC", L"已监控标的（最多 16 个）", 0, 18, 14, 260, 20, -1);
    list_ = AddControl(window_, L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                       18, 36, 624, 186, kList);

    AddControl(window_, L"STATIC", L"交易对", 0, 18, 238, 52, 20, -1);
    symbol_ = AddControl(window_, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                         74, 234, 176, 25, kSymbol);
    SendMessageW(symbol_, EM_SETLIMITTEXT, 32, 0);
    AddControl(window_, L"STATIC", L"市场类型", 0, 270, 238, 65, 20, -1);
    category_ = AddControl(window_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                           340, 234, 178, 200, kCategory);
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"USDT 本位合约"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"USDC 本位合约"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"币本位合约"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"现货"));
    SendMessageW(category_, CB_SETCURSEL, 0, 0);
    AddControl(window_, L"BUTTON", L"添加新标的", WS_TABSTOP | BS_PUSHBUTTON,
               530, 233, 112, 27, kAdd);
    AddControl(window_, L"BUTTON", L"更新选中项", WS_TABSTOP | BS_PUSHBUTTON,
               530, 267, 112, 27, kUpdate);
    AddControl(window_, L"BUTTON", L"删除选中项", WS_TABSTOP | BS_PUSHBUTTON,
               530, 301, 112, 27, kRemove);

    AddControl(window_, L"STATIC", L"合约价格", 0, 18, 282, 65, 20, -1);
    priceType_ = AddControl(window_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                            88, 278, 162, 150, kPriceType);
    SendMessageW(priceType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"最新成交价"));
    SendMessageW(priceType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标记价格"));
    SendMessageW(priceType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"指数价格"));
    SendMessageW(priceType_, CB_SETCURSEL, static_cast<WPARAM>(draft_.priceType), 0);

    AddControl(window_, L"STATIC", L"刷新间隔", 0, 270, 282, 65, 20, -1);
    refresh_ = AddControl(window_, L"EDIT", std::to_wstring(draft_.refreshSeconds).c_str(),
                          WS_BORDER | WS_TABSTOP | ES_NUMBER, 340, 278, 70, 25, kRefresh);
    AddControl(window_, L"STATIC", L"秒（2–300）", 0, 416, 282, 95, 20, -1);
    showChange_ = AddControl(window_, L"BUTTON", L"显示 24 小时涨跌幅",
                             WS_TABSTOP | BS_AUTOCHECKBOX, 18, 318, 190, 24, kShowChange);
    SendMessageW(showChange_, BM_SETCHECK, draft_.showChange ? BST_CHECKED : BST_UNCHECKED, 0);

    status_ = AddControl(window_, L"STATIC", L"输入如 BTCUSDT；BTC/USDT、BTC-USDT 会自动规范化。",
                         0, 18, 354, 500, 22, kStatus);
    AddControl(window_, L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON,
               454, 394, 88, 29, IDOK);
    AddControl(window_, L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
               554, 394, 88, 29, IDCANCEL);
    RefreshList(draft_.instruments.empty() ? -1 : 0);
}

void OptionsWindow::RefreshList(int selection)
{
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const Instrument& instrument : draft_.instruments)
    {
        const std::wstring caption = FormatInstrumentSymbol(instrument) + L"    " +
                                     CategoryCaption(instrument.category);
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str()));
    }
    if (selection >= 0 && selection < static_cast<int>(draft_.instruments.size()))
    {
        SendMessageW(list_, LB_SETCURSEL, selection, 0);
        LoadSelection();
    }
}

int OptionsWindow::SelectedIndex() const
{
    return static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
}

void OptionsWindow::LoadSelection()
{
    const int index = SelectedIndex();
    if (index < 0 || index >= static_cast<int>(draft_.instruments.size())) return;
    const Instrument& instrument = draft_.instruments[static_cast<std::size_t>(index)];
    SetWindowTextW(symbol_, FormatInstrumentSymbol(instrument).c_str());
    SendMessageW(category_, CB_SETCURSEL, CategoryIndex(instrument.category), 0);
    SetStatus(L"修改后点击“更新选中项”；要保留它并增加新标的，请点击“添加新标的”。");
}

Instrument OptionsWindow::ReadInstrument() const
{
    std::array<wchar_t, 64> symbol{};
    GetWindowTextW(symbol_, symbol.data(), static_cast<int>(symbol.size()));
    const int categoryIndex = static_cast<int>(SendMessageW(category_, CB_GETCURSEL, 0, 0));
    return {CategoryAt(categoryIndex), NormalizeSymbol(symbol.data())};
}

void OptionsWindow::AddNew()
{
    Instrument instrument = ReadInstrument();
    if (!IsValidSymbol(instrument.symbol))
    {
        SetStatus(L"交易对格式无效：只能使用 3–32 位英文字母或数字。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const auto duplicate = std::find(draft_.instruments.begin(), draft_.instruments.end(), instrument);
    if (duplicate != draft_.instruments.end())
    {
        SetStatus(L"该标的已经在监控列表中。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (draft_.instruments.size() >= kMaxInstruments)
    {
        SetStatus(L"最多只能同时监控 16 个标的。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.instruments.push_back(std::move(instrument));
    RefreshList(static_cast<int>(draft_.instruments.size() - 1));
    SetStatus(L"新标的已添加；点击“确定”后写入配置并立即刷新。 ");
}

void OptionsWindow::UpdateSelected()
{
    const int selected = SelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(draft_.instruments.size()))
    {
        SetStatus(L"请先在列表中选择要更新的标的。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    Instrument instrument = ReadInstrument();
    if (!IsValidSymbol(instrument.symbol))
    {
        SetStatus(L"交易对格式无效：只能使用 3–32 位英文字母或数字。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const auto duplicate = std::find(draft_.instruments.begin(), draft_.instruments.end(), instrument);
    if (duplicate != draft_.instruments.end() && duplicate != draft_.instruments.begin() + selected)
    {
        SetStatus(L"该标的已经在监控列表中。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.instruments[static_cast<std::size_t>(selected)] = std::move(instrument);
    RefreshList(selected);
    SetStatus(L"列表已更新；点击“确定”后写入配置并立即刷新。 ");
}

void OptionsWindow::RemoveSelected()
{
    const int selected = SelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(draft_.instruments.size()))
    {
        SetStatus(L"请先在列表中选择要删除的标的。 ");
        return;
    }
    draft_.instruments.erase(draft_.instruments.begin() + selected);
    const int next = draft_.instruments.empty() ? -1
        : (std::min)(selected, static_cast<int>(draft_.instruments.size() - 1));
    RefreshList(next);
    if (next < 0) SetWindowTextW(symbol_, L"");
    SetStatus(L"已从列表移除；点击“确定”后生效。 ");
}

void OptionsWindow::Accept()
{
    std::array<wchar_t, 16> refresh{};
    GetWindowTextW(refresh_, refresh.data(), static_cast<int>(refresh.size()));
    wchar_t* end = nullptr;
    const long seconds = std::wcstol(refresh.data(), &end, 10);
    if (end == refresh.data() || *end != L'\0' || seconds < 2 || seconds > 300)
    {
        SetStatus(L"刷新间隔必须是 2 到 300 秒之间的整数。 ");
        SetFocus(refresh_);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.refreshSeconds = static_cast<int>(seconds);
    const int price = static_cast<int>(SendMessageW(priceType_, CB_GETCURSEL, 0, 0));
    draft_.priceType = price == 1 ? PriceType::Mark : price == 2 ? PriceType::Index : PriceType::Last;
    draft_.showChange = SendMessageW(showChange_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    accepted_ = true;
    DestroyWindow(window_);
}

void OptionsWindow::SetStatus(const wchar_t* text)
{
    SetWindowTextW(status_, text);
}
} // namespace bitget
