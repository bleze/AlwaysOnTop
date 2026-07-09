#include <windows.h>

void Aot_SetInstance(HINSTANCE instance);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        Aot_SetInstance(instance);
        DisableThreadLibraryCalls(instance);
    }

    return TRUE;
}