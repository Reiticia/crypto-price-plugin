#include "OptionsWindow.h"

#include "Module.h"
#include "InstrumentValidation.h"
#include "MarketClient.h"

#include <CommCtrl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <string>

namespace cryptoprice
{
namespace
{
constexpr wchar_t kWindowClass[] = L"TrafficMonitorCryptoPriceOptions";
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
constexpr int kDataSource = 1011;
constexpr int kPrecision = 1012;
constexpr UINT_PTR kValidationTimer = 1;

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
    if (category == L"US-STOCK") return L"美股（USD）";
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
    if (category == L"US-STOCK") return 4;
    return 0;
}

std::wstring CategoryAt(int index)
{
    if (index == 1) return L"USDC-FUTURES";
    if (index == 2) return L"COIN-FUTURES";
    if (index == 3) return L"SPOT";
    if (index == 4) return L"US-STOCK";
    return L"USDT-FUTURES";
}
}

bool OptionsWindow::Show(HWND parent, Settings& settings)
{
    OptionsWindow dialog(settings);
    if (!dialog.Create(parent))
    {
        MessageBoxW(parent, L"无法创建选项窗口。", L"Crypto 行情", MB_OK | MB_ICONERROR);
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

    RECT rectangle{0, 0, 660, 480};
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
    window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass, L"Crypto 行情设置",
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
    case WM_TIMER:
        if (wParam == kValidationTimer) FinishValidation();
        return 0;
    case WM_COMMAND:
        if (validation_.valid())
        {
            if (LOWORD(wParam) == IDCANCEL) RequestClose();
            return 0;
        }
        switch (LOWORD(wParam))
        {
        case kList:
            if (HIWORD(wParam) == LBN_SELCHANGE) LoadSelection();
            return 0;
        case kCategory:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                UpdateSourceChoices();
                SetStatus(SendMessageW(category_, CB_GETCURSEL, 0, 0) == 4 ?
                    L"美股固定 Kraken xStocks（USD）；输入 AAPL、TSLA 等代码。" :
                    L"数据源仅作用于当前标的；修改后点击“更新选中项”。");
            }
            return 0;
        case kAdd: AddNew(); return 0;
        case kUpdate: UpdateSelected(); return 0;
        case kRemove: RemoveSelected(); return 0;
        case IDOK: Accept(); return 0;
        case IDCANCEL: RequestClose(); return 0;
        default: break;
        }
        break;
    case WM_CLOSE:
        RequestClose();
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
    AddControl(window_, L"BUTTON", L"标的独立设置（最多 16 个）", BS_GROUPBOX,
               18, 12, 624, 280, -1);
    list_ = AddControl(window_, L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                       30, 36, 600, 134, kList);

    AddControl(window_, L"STATIC", L"交易对", 0, 30, 194, 52, 20, -1);
    symbol_ = AddControl(window_, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                         88, 190, 162, 25, kSymbol);
    SendMessageW(symbol_, EM_SETLIMITTEXT, 32, 0);
    AddControl(window_, L"STATIC", L"市场类型", 0, 270, 194, 65, 20, -1);
    category_ = AddControl(window_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                           340, 190, 178, 200, kCategory);
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"USDT 本位合约"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"USDC 本位合约"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"币本位合约"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"现货"));
    SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"美股（USD）"));
    SendMessageW(category_, CB_SETCURSEL, 0, 0);

    AddControl(window_, L"STATIC", L"数据源", 0, 30, 238, 65, 20, -1);
    dataSource_ = AddControl(window_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                             100, 234, 150, 120, kDataSource);
    UpdateSourceChoices();

    // 小数位数与数据源都只作用于当前标的。
    AddControl(window_, L"STATIC", L"小数位数", 0, 270, 238, 65, 20, -1);
    precision_ = AddControl(window_, L"EDIT", L"",
                            WS_BORDER | WS_TABSTOP | ES_NUMBER,
                            340, 234, 44, 25, kPrecision);
    SendMessageW(precision_, EM_SETLIMITTEXT, 4, 0);
    AddControl(window_, L"STATIC", L"留空自动，0–10 位",
               0, 270, 268, 240, 20, -1);

    AddControl(window_, L"BUTTON", L"添加新标的", WS_TABSTOP | BS_PUSHBUTTON,
               530, 189, 100, 27, kAdd);
    AddControl(window_, L"BUTTON", L"更新选中项", WS_TABSTOP | BS_PUSHBUTTON,
               530, 221, 100, 27, kUpdate);
    AddControl(window_, L"BUTTON", L"删除选中项", WS_TABSTOP | BS_PUSHBUTTON,
               530, 253, 100, 27, kRemove);

    AddControl(window_, L"BUTTON", L"全局设置", BS_GROUPBOX,
               18, 302, 624, 100, -1);
    AddControl(window_, L"STATIC", L"显示价格", 0, 30, 328, 65, 20, -1);
    priceType_ = AddControl(window_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                            100, 324, 230, 150, kPriceType);
    SendMessageW(priceType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"最新价格"));
    SendMessageW(priceType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标记价格（仅合约）"));
    SendMessageW(priceType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"指数价格（仅合约）"));
    SendMessageW(priceType_, CB_SETCURSEL, static_cast<WPARAM>(draft_.priceType), 0);

    AddControl(window_, L"STATIC", L"刷新间隔", 0, 30, 366, 65, 20, -1);
    refresh_ = AddControl(window_, L"EDIT", std::to_wstring(draft_.refreshSeconds).c_str(),
                          WS_BORDER | WS_TABSTOP | ES_NUMBER, 100, 362, 44, 25, kRefresh);
    AddControl(window_, L"STATIC", L"秒（2–300）", 0, 150, 366, 105, 20, -1);
    showChange_ = AddControl(window_, L"BUTTON", L"显示 24 小时涨跌幅",
                             WS_TABSTOP | BS_AUTOCHECKBOX, 270, 362, 360, 24, kShowChange);
    SendMessageW(showChange_, BM_SETCHECK, draft_.showChange ? BST_CHECKED : BST_UNCHECKED, 0);

    status_ = AddControl(window_, L"STATIC", L"输入如 BTCUSDT；BTC/USDT、BTC-USDT 会自动规范化。",
                         0, 18, 412, 624, 22, kStatus);
    AddControl(window_, L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON,
               454, 442, 88, 29, IDOK);
    AddControl(window_, L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
               554, 442, 88, 29, IDCANCEL);
    RefreshList(draft_.instruments.empty() ? -1 : 0);
}

void OptionsWindow::RefreshList(int selection)
{
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const Instrument& instrument : draft_.instruments)
    {
        const std::wstring caption = FormatInstrumentSymbol(instrument) + L"    " +
                                     CategoryCaption(instrument.category) + L"    " + InstrumentSourceName(instrument);
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
    SetWindowTextW(symbol_, (instrument.category == L"US-STOCK" ? instrument.symbol :
                            FormatInstrumentSymbol(instrument)).c_str());
    SendMessageW(category_, CB_SETCURSEL, CategoryIndex(instrument.category), 0);
    UpdateSourceChoices();
    if (instrument.category != L"US-STOCK")
        SendMessageW(dataSource_, CB_SETCURSEL, static_cast<WPARAM>(instrument.dataSource), 0);
    if (instrument.precision >= 0)
        SetWindowTextW(precision_, std::to_wstring(instrument.precision).c_str());
    else
        SetWindowTextW(precision_, L"");
    SetStatus(instrument.category == L"US-STOCK" ?
        L"Kraken xStocks，USD 计价，仅最新价；修改后点击“更新选中项”。" :
        L"修改后点击“更新选中项”；要保留它并增加新标的，请点击“添加新标的”。");
}

void OptionsWindow::UpdateSourceChoices()
{
    const bool stocks = SendMessageW(category_, CB_GETCURSEL, 0, 0) == 4;
    const int previous = static_cast<int>(SendMessageW(dataSource_, CB_GETCURSEL, 0, 0));
    SendMessageW(dataSource_, CB_RESETCONTENT, 0, 0);
    if (stocks)
        SendMessageW(dataSource_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Kraken xStocks"));
    else
        for (const wchar_t* name : {L"Bitget", L"OKX", L"Binance"})
            SendMessageW(dataSource_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(dataSource_, CB_SETCURSEL, stocks ? 0 : (std::max)(0, (std::min)(previous, 2)), 0);
    EnableWindow(dataSource_, !stocks && !validation_.valid());
}

int OptionsWindow::ReadPrecisionInput() const
{
    std::array<wchar_t, 8> precisionText{};
    GetWindowTextW(precision_, precisionText.data(),
                   static_cast<int>(precisionText.size()));
    if (precisionText[0] == L'\0') return -1; // 留空 = 自动推断
    wchar_t* end = nullptr;
    const long value = std::wcstol(precisionText.data(), &end, 10);
    if (end == precisionText.data() || *end != L'\0' || value < 0 || value > 10)
        return -2; // 非法输入
    return static_cast<int>(value);
}

Instrument OptionsWindow::ReadInstrument() const
{
    std::array<wchar_t, 64> symbol{};
    GetWindowTextW(symbol_, symbol.data(), static_cast<int>(symbol.size()));
    const int categoryIndex = static_cast<int>(SendMessageW(category_, CB_GETCURSEL, 0, 0));
    const std::wstring category = CategoryAt(categoryIndex);
    return {category, category == L"US-STOCK" ? NormalizeStockSymbol(symbol.data()) :
            NormalizeSymbol(symbol.data()), ReadPrecisionInput(),
            category == L"US-STOCK" ? DataSource::Kraken :
            static_cast<DataSource>((std::max)(0, static_cast<int>(SendMessageW(dataSource_, CB_GETCURSEL, 0, 0))))};
}

void OptionsWindow::AddNew()
{
    Instrument instrument = ReadInstrument();
    if (!IsValidInstrumentSymbol(instrument))
    {
        SetStatus(L"代码格式无效：美股为 1–16 位字母，其他交易对为 3–32 位字母或数字。");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (instrument.precision == -2)
    {
        SetStatus(L"小数位数必须是 0 到 10 之间的整数，或留空为自动判断。 ");
        SetFocus(precision_);
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
    BeginValidation(&instrument, -1, false);
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
    if (!IsValidInstrumentSymbol(instrument))
    {
        SetStatus(L"代码格式无效：美股为 1–16 位字母，其他交易对为 3–32 位字母或数字。");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (instrument.precision == -2)
    {
        SetStatus(L"小数位数必须是 0 到 10 之间的整数，或留空为自动判断。 ");
        SetFocus(precision_);
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
    BeginValidation(&instrument, selected, false);
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
    if (next < 0) SetWindowTextW(precision_, L"");
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
    BeginValidation(nullptr, SelectedIndex(), true);
}

void OptionsWindow::BeginValidation(const Instrument* instrument, int selection, bool accept)
{
    pendingSettings_ = draft_;
    const int price = static_cast<int>(SendMessageW(priceType_, CB_GETCURSEL, 0, 0));
    pendingSettings_.priceType = price == 1 ? PriceType::Mark : price == 2 ? PriceType::Index : PriceType::Last;
    pendingSettings_.showChange = SendMessageW(showChange_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (instrument)
    {
        if (selection < 0)
        {
            selection = static_cast<int>(pendingSettings_.instruments.size());
            pendingSettings_.instruments.push_back(*instrument);
        }
        else
            pendingSettings_.instruments[static_cast<std::size_t>(selection)] = *instrument;
    }
    Settings validationSettings = pendingSettings_;
    if (instrument) validationSettings.instruments = {*instrument};
    pendingSelection_ = selection;
    pendingAccept_ = accept;
    if (!SetTimer(window_, kValidationTimer, 100, nullptr))
    {
        SetStatus(L"无法启动标的校验，请重试。");
        return;
    }
    try
    {
        validation_ = std::async(std::launch::async, [settings = std::move(validationSettings)] {
            MarketClient client;
            return ValidateInstruments(settings,
                [&client](DataSource dataSource, const std::wstring& category, PriceType priceType,
                          std::unordered_map<std::string, Ticker>& tickers, std::wstring& error) {
                    return client.FetchCategory(dataSource, category, priceType, tickers, error);
                });
        });
    }
    catch (...)
    {
        KillTimer(window_, kValidationTimer);
        SetStatus(L"无法启动标的校验，请重试。");
        return;
    }
    SetValidationBusy(true);
    SetStatus(L"正在查询并校验标的价格，请稍候……");
}

void OptionsWindow::FinishValidation()
{
    if (!validation_.valid() ||
        validation_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    KillTimer(window_, kValidationTimer);
    std::wstring error;
    try { error = validation_.get(); }
    catch (...) { error = L"标的校验失败，未应用更改，请稍后重试。"; }
    SetValidationBusy(false);
    if (closeRequested_)
    {
        DestroyWindow(window_);
        return;
    }
    if (!error.empty())
    {
        SetStatus(L"校验未通过，未应用更改。请查看提示并重试。");
        MessageBoxW(window_, error.c_str(), L"标的校验失败", MB_OK | MB_ICONWARNING);
        return;
    }
    draft_ = std::move(pendingSettings_);
    if (pendingAccept_)
    {
        accepted_ = true;
        DestroyWindow(window_);
        return;
    }
    RefreshList(pendingSelection_);
    SetStatus(L"标的价格校验通过，列表已更新；点击“确定”后保存。");
}

void OptionsWindow::SetValidationBusy(bool busy)
{
    for (int id : {kList, kSymbol, kCategory, kAdd, kRemove, kPriceType, kRefresh,
                   kShowChange, kUpdate, kDataSource, kPrecision, IDOK})
        EnableWindow(GetDlgItem(window_, id), !busy);
    if (!busy && SendMessageW(category_, CB_GETCURSEL, 0, 0) == 4)
        EnableWindow(dataSource_, FALSE);
}

void OptionsWindow::RequestClose()
{
    if (validation_.valid())
    {
        // 保持消息循环运行，待后台请求结束后安全销毁；不提交校验结果。
        closeRequested_ = true;
        SetStatus(L"已取消更改，正在等待行情查询结束后关闭……");
        return;
    }
    DestroyWindow(window_);
}

void OptionsWindow::SetStatus(const wchar_t* text)
{
    SetWindowTextW(status_, text);
}
} // namespace cryptoprice
