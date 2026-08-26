#include "PluginInterface.h"

#include <Windows.h>

#include <iostream>
#include <string>

namespace
{
bool HasTwoDecimalPrice(const std::wstring& value)
{
    const std::size_t separator = value.find(L' ');
    const std::wstring price = value.substr(0, separator);
    const std::size_t decimal = price.rfind(L'.');
    return decimal != std::wstring::npos && price.size() - decimal - 1 == 2;
}
}

int wmain(int argumentCount, wchar_t** arguments)
{
    if (argumentCount < 2 || argumentCount > 4)
    {
        std::cerr << "Usage: CryptoPriceSmoke <plugin.dll> [config-dir] [--live|--no-change]\n";
        return 2;
    }
    HMODULE module = LoadLibraryW(arguments[1]);
    if (!module)
    {
        std::cerr << "LoadLibrary failed: " << GetLastError() << '\n';
        return 1;
    }
    using GetInstance = ITMPlugin* (*)();
    const auto getInstance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
    ITMPlugin* plugin = getInstance ? getInstance() : nullptr;
    bool valid = plugin && plugin->GetAPIVersion() == 7 && plugin->GetItem(0) &&
                 plugin->GetItem(0)->GetItemId() && plugin->GetInfo(ITMPlugin::TMI_NAME);
    bool live = false;
    bool expectNoChange = false;
    for (int index = 2; valid && index < argumentCount; ++index)
    {
        if (std::wstring(arguments[index]) == L"--live")
        {
            live = true;
            continue;
        }
        if (std::wstring(arguments[index]) == L"--no-change")
        {
            expectNoChange = true;
            continue;
        }
        plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, arguments[index]);
        const std::wstring aggregate = plugin->GetItem(0)->GetItemValueText();
        valid = plugin->GetItem(1) && plugin->GetItem(2) && !plugin->GetItem(3) &&
                aggregate.find(L"BTCUSDT") != std::wstring::npos &&
                aggregate.find(L"ETH/USDT") != std::wstring::npos;
    }
    if (valid && plugin->GetItem(1))
    {
        for (int index = 0; index < 3; ++index)
        {
            const std::wstring sample = plugin->GetItem(index)->GetItemValueSampleText();
            const bool reservesChangeWidth = sample.find(L'%') != std::wstring::npos;
            if (reservesChangeWidth == expectNoChange)
            {
                std::wcerr << L"24-hour change sample width does not match configuration\n";
                valid = false;
                break;
            }
        }
    }
    if (valid)
        std::wcout << L"Loaded: " << plugin->GetInfo(ITMPlugin::TMI_NAME) << L" (API "
                   << plugin->GetAPIVersion() << L")\n";
    else
        std::cerr << "Plugin ABI smoke check failed\n";
    if (valid && live)
    {
        plugin->DataRequired();
        constexpr int liveAttempts = 40;
        for (int attempt = 0; attempt < liveAttempts; ++attempt)
        {
            Sleep(500);
            plugin->DataRequired();
            const std::wstring value = plugin->GetItem(0)->GetItemValueText();
            const std::wstring firstPrice = plugin->GetItem(1)->GetItemValueText();
            const std::wstring secondPrice = plugin->GetItem(2)
                ? plugin->GetItem(2)->GetItemValueText() : firstPrice;
            if (firstPrice != L"--" && secondPrice != L"--")
            {
                if (!HasTwoDecimalPrice(firstPrice) || !HasTwoDecimalPrice(secondPrice))
                {
                    std::wcerr << L"Price is not formatted with two decimals\n";
                    valid = false;
                }
                else
                {
                    std::wcout << L"Live aggregate: " << value << L'\n';
                }
                break;
            }
            if (attempt == liveAttempts - 1)
            {
                std::wcerr << L"Live quote check failed: " << plugin->GetTooltipInfo() << L'\n';
                valid = false;
            }
        }
    }
    FreeLibrary(module);
    return valid ? 0 : 1;
}
