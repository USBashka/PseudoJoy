#include "AppWindow.h"

#include <Windows.h>
#include <objbase.h>
#include <shellscalingapi.h>

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, const int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\PseudoJoy.Singleton");
    if (mutex == nullptr) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const HWND existing = FindWindowW(L"PseudoJoy.MainWindow", nullptr); existing != nullptr) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    AppWindow application;
    const int result = application.Run(instance, showCommand);
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}

