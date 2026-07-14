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

constexpr std::uint32_t kKeyW = 1U << 0;
constexpr std::uint32_t kKeyA = 1U << 1;
constexpr std::uint32_t kKeyS = 1U << 2;
constexpr std::uint32_t kKeyD = 1U << 3;
constexpr std::uint32_t kKeyUp = 1U << 4;
constexpr std::uint32_t kKeyLeft = 1U << 5;
constexpr std::uint32_t kKeyDown = 1U << 6;
constexpr std::uint32_t kKeyRight = 1U << 7;

std::uint32_t MovementKeyBit(const UINT virtualKey) noexcept {
    switch (virtualKey) {
        case 'W': return kKeyW;
        case 'A': return kKeyA;
        case 'S': return kKeyS;
        case 'D': return kKeyD;
        case VK_UP: return kKeyUp;
        case VK_LEFT: return kKeyLeft;
        case VK_DOWN: return kKeyDown;
        case VK_RIGHT: return kKeyRight;
        default: return 0;
    }
}

float Length(const Vector2 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

Vector2 LimitToUnitCircle(Vector2 value) noexcept {
    const float length = Length(value);
    if (length > 1.0f) {
        value.x /= length;
        value.y /= length;
    }
    return value;
}

Vector2 ReadKeyboardActions(const std::uint32_t keyMask) noexcept {
    const float negativeX = (keyMask & (kKeyA | kKeyLeft)) != 0 ? 1.0f : 0.0f;
    const float positiveX = (keyMask & (kKeyD | kKeyRight)) != 0 ? 1.0f : 0.0f;
    const float negativeY = (keyMask & (kKeyW | kKeyUp)) != 0 ? 1.0f : 0.0f;
    const float positiveY = (keyMask & (kKeyS | kKeyDown)) != 0 ? 1.0f : 0.0f;
    return {positiveX - negativeX, positiveY - negativeY};
}

Vector2 GetVector(const Vector2 keyboard, const Vector2 analog) noexcept {
    const float negativeX = std::max(std::max(0.0f, -keyboard.x), std::max(0.0f, -analog.x));
    const float positiveX = std::max(std::max(0.0f, keyboard.x), std::max(0.0f, analog.x));
    const float negativeY = std::max(std::max(0.0f, -keyboard.y), std::max(0.0f, -analog.y));
    const float positiveY = std::max(std::max(0.0f, keyboard.y), std::max(0.0f, analog.y));
    return LimitToUnitCircle({positiveX - negativeX, positiveY - negativeY});
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

void UpdateFilteredAnalog(Vector2& filtered, const Vector2 target,
                          const float deltaSeconds, const int smoothingMilliseconds) noexcept {
    if (Length(target) <= 0.001f) {
        filtered = {};
        return;
    }

    if (smoothingMilliseconds <= 0) {
        filtered = target;
        return;
    }

    const float alpha = 1.0f - std::exp(
        -deltaSeconds / (static_cast<float>(smoothingMilliseconds) / 1000.0f));
    filtered.x += (target.x - filtered.x) * alpha;
    filtered.y += (target.y - filtered.y) * alpha;
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

void SendRelativeMove(const LONG x, const LONG y) noexcept {
    if (x == 0 && y == 0) {
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = x;
    input.mi.dy = y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
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
    const std::uint32_t bit = MovementKeyBit(virtualKey);
    if (bit == 0) {
        return;
    }

    if (pressed) {
        keyMask_.fetch_or(bit, std::memory_order_release);
    } else {
        keyMask_.fetch_and(~bit, std::memory_order_release);
    }
}

bool InputEngine::ShouldBlockKey(const UINT virtualKey) const noexcept {
    return IsMovementKey(virtualKey) && IsCapturing() &&
           blockKeyboard_.load(std::memory_order_relaxed);
}

void InputEngine::SetRadius(const int pixels) noexcept {
    radius_.store(std::clamp(pixels, 16, 300), std::memory_order_relaxed);
}

void InputEngine::SetDeadZonePercent(const int percent) noexcept {
    deadZonePercent_.store(std::clamp(percent, 0, 45), std::memory_order_relaxed);
}

void InputEngine::SetSmoothingMilliseconds(const int milliseconds) noexcept {
    smoothingMilliseconds_.store(std::clamp(milliseconds, 0, 250), std::memory_order_relaxed);
}

void InputEngine::SetPressDelayMilliseconds(const int milliseconds) noexcept {
    pressDelayMilliseconds_.store(std::clamp(milliseconds, 0, 120), std::memory_order_relaxed);
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

int InputEngine::PressDelayMilliseconds() const noexcept {
    return pressDelayMilliseconds_.load(std::memory_order_relaxed);
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
    return MovementKeyBit(virtualKey) != 0;
}

void InputEngine::Worker(const std::stop_token stopToken) {
    using namespace std::chrono_literals;

    XInputApi xinput;
    bool mouseHeld = false;
    bool controllerClickHeld = false;
    bool controllerClickButtonWasDown = false;
    POINT anchor{};
    Vector2 filteredAnalog{};
    Vector2 filteredRightStick{};
    float cursorRemainderX = 0.0f;
    float cursorRemainderY = 0.0f;
    std::chrono::steady_clock::time_point pressStarted{};
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
        constexpr WORD kControllerClickButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_RIGHT_SHOULDER;
        const bool controllerClickButtonDown = controllerConnected &&
            (controllerState.Gamepad.wButtons & kControllerClickButtons) != 0;

        if (!capturing_.load(std::memory_order_acquire)) {
            if (mouseHeld) {
                ReleaseMouseAt(anchor);
                mouseHeld = false;
            }
            if (controllerClickHeld) {
                SendButton(MOUSEEVENTF_LEFTUP);
                controllerClickHeld = false;
            }
            filteredAnalog = {};
            filteredRightStick = {};
            cursorRemainderX = 0.0f;
            cursorRemainderY = 0.0f;
            controllerClickButtonWasDown = controllerClickButtonDown;
            telemetryMouseHeld_.store(false, std::memory_order_relaxed);
            telemetryX_.store(0, std::memory_order_relaxed);
            telemetryY_.store(0, std::memory_order_relaxed);
            std::this_thread::sleep_for(8ms);
            continue;
        }

        const std::uint32_t keyboardSnapshot = keyMask_.load(std::memory_order_acquire);
        const Vector2 keyboard = ReadKeyboardActions(keyboardSnapshot);

        Vector2 analog{};
        Vector2 rightStick{};
        if (controllerConnected) {
            analog.x = NormalizeAxis(controllerState.Gamepad.sThumbLX);
            analog.y = -NormalizeAxis(controllerState.Gamepad.sThumbLY);
            rightStick.x = NormalizeAxis(controllerState.Gamepad.sThumbRX);
            rightStick.y = -NormalizeAxis(controllerState.Gamepad.sThumbRY);
            const float deadZone = static_cast<float>(DeadZonePercent()) / 100.0f;
            analog = ApplyRadialDeadZone(analog, deadZone);
            rightStick = ApplyRadialDeadZone(rightStick, deadZone);
        }

        const int smoothingMs = SmoothingMilliseconds();
        UpdateFilteredAnalog(filteredAnalog, analog, deltaSeconds, smoothingMs);
        UpdateFilteredAnalog(filteredRightStick, rightStick, deltaSeconds, smoothingMs);

        const Vector2 inputVector = GetVector(keyboard, filteredAnalog);
        const bool active = Length(inputVector) > 0.002f;
        if (active) {
            if (controllerClickHeld) {
                SendButton(MOUSEEVENTF_LEFTUP);
                controllerClickHeld = false;
            }
            if (!mouseHeld) {
                GetCursorPos(&anchor);
                SendButton(MOUSEEVENTF_LEFTDOWN);
                mouseHeld = true;
                pressStarted = tick;
            }

            const auto heldFor = std::chrono::duration_cast<std::chrono::milliseconds>(
                tick - pressStarted);
            if (heldFor.count() >= PressDelayMilliseconds()) {
                const int radius = Radius();
                POINT destination{
                    anchor.x + static_cast<LONG>(std::lround(inputVector.x * static_cast<float>(radius))),
                    anchor.y + static_cast<LONG>(std::lround(inputVector.y * static_cast<float>(radius))),
                };
                SendMove(destination);
            }
        } else if (mouseHeld) {
            ReleaseMouseAt(anchor);
            mouseHeld = false;
        }

        if (!mouseHeld) {
            constexpr float kRightStickPixelsPerSecond = 900.0f;
            if (Length(filteredRightStick) <= 0.001f) {
                cursorRemainderX = 0.0f;
                cursorRemainderY = 0.0f;
            } else {
                cursorRemainderX += filteredRightStick.x * kRightStickPixelsPerSecond * deltaSeconds;
                cursorRemainderY += filteredRightStick.y * kRightStickPixelsPerSecond * deltaSeconds;
                const auto moveX = static_cast<LONG>(cursorRemainderX);
                const auto moveY = static_cast<LONG>(cursorRemainderY);
                cursorRemainderX -= static_cast<float>(moveX);
                cursorRemainderY -= static_cast<float>(moveY);
                SendRelativeMove(moveX, moveY);
            }

            if (controllerClickButtonDown && !controllerClickButtonWasDown && !controllerClickHeld) {
                SendButton(MOUSEEVENTF_LEFTDOWN);
                controllerClickHeld = true;
            } else if (!controllerClickButtonDown && controllerClickHeld) {
                SendButton(MOUSEEVENTF_LEFTUP);
                controllerClickHeld = false;
            }
        } else {
            cursorRemainderX = 0.0f;
            cursorRemainderY = 0.0f;
        }
        controllerClickButtonWasDown = controllerClickButtonDown;

        telemetryMouseHeld_.store(mouseHeld || controllerClickHeld, std::memory_order_relaxed);
        telemetryX_.store(static_cast<int>(std::lround(inputVector.x * 1000.0f)), std::memory_order_relaxed);
        telemetryY_.store(static_cast<int>(std::lround(inputVector.y * 1000.0f)), std::memory_order_relaxed);

        std::this_thread::sleep_for(8ms);
    }

    if (mouseHeld) {
        ReleaseMouseAt(anchor);
    }
    if (controllerClickHeld) {
        SendButton(MOUSEEVENTF_LEFTUP);
    }
    telemetryMouseHeld_.store(false, std::memory_order_relaxed);
    telemetryX_.store(0, std::memory_order_relaxed);
    telemetryY_.store(0, std::memory_order_relaxed);
}
