#include "Module.h"
#include "StreamClient.h"

HINSTANCE g_moduleInstance = nullptr;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_moduleInstance = module;
        DisableThreadLibraryCalls(module);
    }
    if (reason == DLL_PROCESS_DETACH) cryptoprice::g_processStopping.store(true);
    return TRUE;
}
