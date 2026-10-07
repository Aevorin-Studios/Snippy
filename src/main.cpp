#include "app.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
    // Single instance check
    HANDLE hMutexOld = CreateMutexW(nullptr, TRUE, L"Snippy_SingleInstance_Mutex_9A8B7C");
    DWORD gleOld = GetLastError();
    HANDLE hMutexNew = CreateMutexW(nullptr, TRUE, L"Snippy_SingleInstance_Mutex");
    DWORD gleNew = GetLastError();
    if (gleOld == ERROR_ALREADY_EXISTS || gleNew == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Snippy is already running.", L"Snippy", MB_OK | MB_ICONINFORMATION);
        if (hMutexOld) CloseHandle(hMutexOld);
        if (hMutexNew) CloseHandle(hMutexNew);
        return 0;
    }

    if (!snippy::g_app.Initialize(hInstance)) {
        MessageBoxW(nullptr, 
            L"Failed to initialize Snippy.\n\n"
            L"Please ensure your system supports DirectX 11 and Windows 10+.", 
            L"Snippy Error", MB_OK | MB_ICONERROR);
        if (hMutexOld) { ReleaseMutex(hMutexOld); CloseHandle(hMutexOld); }
        if (hMutexNew) { ReleaseMutex(hMutexNew); CloseHandle(hMutexNew); }
        return 1;
    }

    int ret = snippy::g_app.Run();
    
    snippy::g_app.Shutdown();
    if (hMutexOld) { ReleaseMutex(hMutexOld); CloseHandle(hMutexOld); }
    if (hMutexNew) { ReleaseMutex(hMutexNew); CloseHandle(hMutexNew); }
    
    return ret;
}
