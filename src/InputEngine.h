#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

struct InputTelemetry {
    bool capturing = false;
    bool mouseHeld = false;
    bool controllerConnected = false;
    int controllerIndex = -1;
    float x = 0.0f;
    float y = 0.0f;
};

class InputEngine final {
public:
    InputEngine();
    ~InputEngine();

    InputEngine(const InputEngine&) = delete;
    InputEngine& operator=(const InputEngine&) = delete;

    void Start();
    void SetCapturing(bool enabled);
    void ToggleCapturing();
    [[nodiscard]] bool IsCapturing() const noexcept;

    void SetKeyState(UINT virtualKey, bool pressed) noexcept;
    [[nodiscard]] bool ShouldBlockKey(UINT virtualKey) const noexcept;

    void SetRadius(int pixels) noexcept;
    void SetDeadZonePercent(int percent) noexcept;
    void SetSmoothingMilliseconds(int milliseconds) noexcept;
    void SetBlockKeyboard(bool enabled) noexcept;

    [[nodiscard]] int Radius() const noexcept;
    [[nodiscard]] int DeadZonePercent() const noexcept;
    [[nodiscard]] int SmoothingMilliseconds() const noexcept;
    [[nodiscard]] bool BlockKeyboard() const noexcept;
    [[nodiscard]] InputTelemetry Telemetry() const noexcept;

private:
    static bool IsMovementKey(UINT virtualKey) noexcept;
    static int KeyIndex(UINT virtualKey) noexcept;
    void Worker(std::stop_token stopToken);

    std::array<std::atomic_bool, 8> keys_{};
    std::atomic_bool capturing_{false};
    std::atomic_int radius_{100};
    std::atomic_int deadZonePercent_{18};
    std::atomic_int smoothingMilliseconds_{65};
    std::atomic_bool blockKeyboard_{true};

    std::atomic_bool telemetryMouseHeld_{false};
    std::atomic_bool telemetryControllerConnected_{false};
    std::atomic_int telemetryControllerIndex_{-1};
    std::atomic_int telemetryX_{0};
    std::atomic_int telemetryY_{0};

    std::jthread worker_;
};

