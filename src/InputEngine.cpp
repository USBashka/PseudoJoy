#include "InputEngine.h"

#include <Xinput.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
using XInputGetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

struct XInputApi {
    HMODULE module = nullptr;
    XInputGetStateFunction getState = nullptr;

    XInputApi() {
        constexpr const wchar_t* libraries[] = {
            L"xinput1_4.dll",
            L"xinput1_3.dll",
            L"xinput9_1_0.dll",
        };

        for (const auto* library : libraries) {
            module = LoadLibraryW(library);
            if (module != nullptr) {
                getState = reinterpret_cast<XInputGetStateFunction>(
                    GetProcAddress(module, "XInputGetState"));
                if (getState != nullptr) {
                    break;
                }
                FreeLibrary(module);
                module = nullptr;
            }
        }
    }

    ~XInputApi() {
        if (module != nullptr) {
            FreeLibrary(module);
        }
    }
};

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

float Length(const Vector2 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

Vector2 NormalizeKeyboard(Vector2 value) noexcept {
    const float length = Length(value);
    if (length > 1.0f) {
        value.x /= length;
        value.y /= length;
    }
    return value;
}

float NormalizeAxis(const SHORT value) noexcept {
    return value >= 0 ? static_cast<float>(value) / 32767.0f
                      : static_cast<float>(value) / 32768.0f;
}

Vector2 ApplyRadialDeadZone(Vector2 value, const float deadZone) noexcept {
    const float magnitude = Length(value);
    if (magnitude <= deadZone || magnitude <= 0.0001f) {
        return {};
    }

    const float remapped = std::clamp((magnitude - deadZone) / (1.0f - deadZone), 0.0f, 1.0f);
    const float scale = remapped / magnitude;
    return {value.x * scale, value.y * scale};
}

POINT ClampToVirtualDesktop(POINT point) noexcept {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int height = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    point.x = std::clamp(point.x, static_cast<LONG>(left), static_cast<LONG>(left + width - 1));
    point.y = std::clamp(point.y, static_cast<LONG>(top), static_cast<LONG>(top + height - 1));
    return point;
}

INPUT MakeAbsoluteMove(const POINT point) noexcept {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int height = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(
        std::llround(static_cast<double>(point.x - left) * 65535.0 / static_cast<double>(width - 1)));
    input.mi.dy = static_cast<LONG>(
        std::llround(static_cast<double>(point.y - top) * 65535.0 / static_cast<double>(height - 1)));
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return input;
}

void SendMove(const POINT point) noexcept {
    INPUT input = MakeAbsoluteMove(ClampToVirtualDesktop(point));
    SendInput(1, &input, sizeof(INPUT));
}

void SendButton(const DWORD flag) noexcept {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    SendInput(1, &input, sizeof(INPUT));
}

void ReleaseMouseAt(const POINT anchor) noexcept {
    INPUT inputs[2]{};
    inputs[0] = MakeAbsoluteMove(ClampToVirtualDesktop(anchor));
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));
}
}  // namespace

InputEngine::InputEngine() = default;

InputEngine::~InputEngine() {
    capturing_.store(false, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

void InputEngine::Start() {
    if (!worker_.joinable()) {
        worker_ = std::jthread([this](const std::stop_token token) { Worker(token); });
    }
}

void InputEngine::SetCapturing(const bool enabled) {
    capturing_.store(enabled, std::memory_order_release);
}

void InputEngine::ToggleCapturing() {
    SetCapturing(!IsCapturing());
}

bool InputEngine::IsCapturing() const noexcept {
    return capturing_.load(std::memory_order_acquire);
}

void InputEngine::SetKeyState(const UINT virtualKey, const bool pressed) noexcept {
    const int index = KeyIndex(virtualKey);
    if (index >= 0) {
        keys_[static_cast<std::size_t>(index)].store(pressed, std::memory_order_release);
    }
}

bool InputEngine::ShouldBlockKey(const UINT virtualKey) const noexcept {
    return IsMovementKey(virtualKey) && IsCapturing() &&
           blockKeyboard_.load(std::memory_order_relaxed);
}

void InputEngine::SetRadius(const int pixels) noexcept {
    radius_.store(std::clamp(pixels, 40, 300), std::memory_order_relaxed);
}

void InputEngine::SetDeadZonePercent(const int percent) noexcept {
    deadZonePercent_.store(std::clamp(percent, 0, 45), std::memory_order_relaxed);
}

void InputEngine::SetSmoothingMilliseconds(const int milliseconds) noexcept {
    smoothingMilliseconds_.store(std::clamp(milliseconds, 0, 250), std::memory_order_relaxed);
}

void InputEngine::SetBlockKeyboard(const bool enabled) noexcept {
    blockKeyboard_.store(enabled, std::memory_order_relaxed);
}

int InputEngine::Radius() const noexcept {
    return radius_.load(std::memory_order_relaxed);
}

int InputEngine::DeadZonePercent() const noexcept {
    return deadZonePercent_.load(std::memory_order_relaxed);
}

int InputEngine::SmoothingMilliseconds() const noexcept {
    return smoothingMilliseconds_.load(std::memory_order_relaxed);
}

bool InputEngine::BlockKeyboard() const noexcept {
    return blockKeyboard_.load(std::memory_order_relaxed);
}

InputTelemetry InputEngine::Telemetry() const noexcept {
    InputTelemetry telemetry;
    telemetry.capturing = IsCapturing();
    telemetry.mouseHeld = telemetryMouseHeld_.load(std::memory_order_relaxed);
    telemetry.controllerConnected = telemetryControllerConnected_.load(std::memory_order_relaxed);
    telemetry.controllerIndex = telemetryControllerIndex_.load(std::memory_order_relaxed);
    telemetry.x = static_cast<float>(telemetryX_.load(std::memory_order_relaxed)) / 1000.0f;
    telemetry.y = static_cast<float>(telemetryY_.load(std::memory_order_relaxed)) / 1000.0f;
    return telemetry;
}

bool InputEngine::IsMovementKey(const UINT virtualKey) noexcept {
    return KeyIndex(virtualKey) >= 0;
}

int InputEngine::KeyIndex(const UINT virtualKey) noexcept {
    switch (virtualKey) {
        case 'W': return 0;
        case 'A': return 1;
        case 'S': return 2;
        case 'D': return 3;
        case VK_UP: return 4;
        case VK_LEFT: return 5;
        case VK_DOWN: return 6;
        case VK_RIGHT: return 7;
        default: return -1;
    }
}

void InputEngine::Worker(const std::stop_token stopToken) {
    using namespace std::chrono_literals;

    XInputApi xinput;
    bool mouseHeld = false;
    POINT anchor{};
    Vector2 filtered{};
    auto previousTick = std::chrono::steady_clock::now();

    while (!stopToken.stop_requested()) {
        const auto tick = std::chrono::steady_clock::now();
        const float deltaSeconds = std::clamp(
            std::chrono::duration<float>(tick - previousTick).count(), 0.001f, 0.050f);
        previousTick = tick;

        XINPUT_STATE controllerState{};
        bool controllerConnected = false;
        int controllerIndex = -1;
        if (xinput.getState != nullptr) {
            const int preferred = telemetryControllerIndex_.load(std::memory_order_relaxed);
            if (preferred >= 0 && preferred < XUSER_MAX_COUNT &&
                xinput.getState(static_cast<DWORD>(preferred), &controllerState) == ERROR_SUCCESS) {
                controllerConnected = true;
                controllerIndex = preferred;
            } else {
                for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
                    if (xinput.getState(index, &controllerState) == ERROR_SUCCESS) {
                        controllerConnected = true;
                        controllerIndex = static_cast<int>(index);
                        break;
                    }
                }
            }
        }

        telemetryControllerConnected_.store(controllerConnected, std::memory_order_relaxed);
        telemetryControllerIndex_.store(controllerIndex, std::memory_order_relaxed);

        if (!capturing_.load(std::memory_order_acquire)) {
            if (mouseHeld) {
                ReleaseMouseAt(anchor);
                mouseHeld = false;
            }
            filtered = {};
            telemetryMouseHeld_.store(false, std::memory_order_relaxed);
            telemetryX_.store(0, std::memory_order_relaxed);
            telemetryY_.store(0, std::memory_order_relaxed);
            std::this_thread::sleep_for(8ms);
            continue;
        }

        Vector2 keyboard{
            static_cast<float>((keys_[3].load() || keys_[7].load()) -
                               (keys_[1].load() || keys_[5].load())),
            static_cast<float>((keys_[2].load() || keys_[6].load()) -
                               (keys_[0].load() || keys_[4].load())),
        };
        keyboard = NormalizeKeyboard(keyboard);

        Vector2 analog{};
        if (controllerConnected) {
            analog.x = NormalizeAxis(controllerState.Gamepad.sThumbLX);
            analog.y = -NormalizeAxis(controllerState.Gamepad.sThumbLY);
            const float deadZone = static_cast<float>(DeadZonePercent()) / 100.0f;
            analog = ApplyRadialDeadZone(analog, deadZone);
        }

        const bool keyboardActive = Length(keyboard) > 0.001f;
        const Vector2 target = keyboardActive ? keyboard : analog;
        const float targetLength = Length(target);

        if (keyboardActive || targetLength <= 0.001f) {
            filtered = target;
        } else {
            const int smoothingMs = SmoothingMilliseconds();
            if (smoothingMs <= 0) {
                filtered = target;
            } else {
                const float alpha = 1.0f - std::exp(
                    -deltaSeconds / (static_cast<float>(smoothingMs) / 1000.0f));
                filtered.x += (target.x - filtered.x) * alpha;
                filtered.y += (target.y - filtered.y) * alpha;
            }
        }

        const bool active = Length(filtered) > 0.002f;
        if (active) {
            if (!mouseHeld) {
                GetCursorPos(&anchor);
                SendButton(MOUSEEVENTF_LEFTDOWN);
                mouseHeld = true;
            }

            const int radius = Radius();
            POINT destination{
                anchor.x + static_cast<LONG>(std::lround(filtered.x * static_cast<float>(radius))),
                anchor.y + static_cast<LONG>(std::lround(filtered.y * static_cast<float>(radius))),
            };
            SendMove(destination);
        } else if (mouseHeld) {
            ReleaseMouseAt(anchor);
            mouseHeld = false;
        }

        telemetryMouseHeld_.store(mouseHeld, std::memory_order_relaxed);
        telemetryX_.store(static_cast<int>(std::lround(filtered.x * 1000.0f)), std::memory_order_relaxed);
        telemetryY_.store(static_cast<int>(std::lround(filtered.y * 1000.0f)), std::memory_order_relaxed);

        std::this_thread::sleep_for(8ms);
    }

    if (mouseHeld) {
        ReleaseMouseAt(anchor);
    }
    telemetryMouseHeld_.store(false, std::memory_order_relaxed);
    telemetryX_.store(0, std::memory_order_relaxed);
    telemetryY_.store(0, std::memory_order_relaxed);
}
