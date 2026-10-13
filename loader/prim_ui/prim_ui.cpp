#include <windows.h>

extern "C" __declspec(dllexport) bool __stdcall InitUIEngine() {
    // Dummy function that the loader will call to verify integrity
    return true;
}

extern "C" __declspec(dllexport) int __stdcall GetThemeVersion() {
    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
