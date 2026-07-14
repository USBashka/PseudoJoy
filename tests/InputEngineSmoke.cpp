#include "../src/InputEngine.h"

#include <Windows.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
bool Near(const POINT actual, const POINT expected, const int tolerance = 5) noexcept {
    return std::abs(actual.x - expected.x) <= tolerance &&
           std::abs(actual.y - expected.y) <= tolerance;
}

bool CheckCursor(const wchar_t* step, const POINT expected) {
    POINT actual{};
    GetCursorPos(&actual);
    if (!Near(actual, expected)) {
        std::wcerr << L"[FAIL] " << step << L": ожидалось (" << expected.x << L", "
                   << expected.y << L"), получено (" << actual.x << L", " << actual.y << L")\n";
        return false;
    }
    return true;
}
}  // namespace

int wmain() {
    using namespace std::chrono_literals;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int width = 420;
    const int height = 360;
    const int left = workArea.left + (workArea.right - workArea.left - width) / 2;
    const int top = workArea.top + (workArea.bottom - workArea.top - height) / 2;
    const HWND surface = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"PseudoJoy input smoke test",
        WS_POPUP | WS_VISIBLE, left, top, width, height, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (surface == nullptr) {
        std::wcerr << L"[FAIL] Не удалось создать безопасную тестовую поверхность.\n";
        return 1;
    }

    const POINT anchor{left + width / 2, top + height / 2};
    SetCursorPos(anchor.x, anchor.y);

    bool success = true;
    {
        InputEngine engine;
        engine.SetRadius(100);
        engine.Start();
        engine.SetCapturing(true);

        engine.SetKeyState('W', true);
        std::this_thread::sleep_for(80ms);
        success &= CheckCursor(L"W", POINT{anchor.x, anchor.y - 100});
        success &= engine.Telemetry().mouseHeld;

        engine.SetKeyState('D', true);
        std::this_thread::sleep_for(80ms);
        success &= CheckCursor(L"WD", POINT{anchor.x + 71, anchor.y - 71});

        engine.SetKeyState('W', false);
        std::this_thread::sleep_for(80ms);
        success &= CheckCursor(L"D после WD", POINT{anchor.x + 100, anchor.y});

        engine.SetKeyState('D', false);
        std::this_thread::sleep_for(80ms);
        success &= CheckCursor(L"отпускание", anchor);
        success &= !engine.Telemetry().mouseHeld;

        engine.SetCapturing(false);
    }

    DestroyWindow(surface);
    if (!success) {
        std::wcerr << L"[FAIL] Дымовой тест ввода не пройден.\n";
        return 2;
    }

    std::wcout << L"[OK] W, WD, смена направления и возврат курсора работают.\n";
    return 0;
}

