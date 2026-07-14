#pragma once

#include "InputEngine.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

class AppWindow final {
public:
    AppWindow();
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    int Run(HINSTANCE instance, int showCommand);

private:
    enum class DragTarget {
        None,
        Radius,
        DeadZone,
        Smoothing,
    };

    static constexpr UINT_PTR kUiTimer = 1;
    static constexpr int kHotkeyId = 1;
    static constexpr UINT kFallbackToggleMessage = WM_APP + 1;

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK KeyboardProcedure(int code, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool Create(HINSTANCE instance, int showCommand);
    bool CreateGraphicsResources();
    void DiscardGraphicsResources();
    void Paint();
    void Resize(UINT width, UINT height);
    void ApplyBackdrop();
    void LoadSettings();
    void SaveSettings() const;

    void ToggleCapture();
    void UpdateSlider(DragTarget target, float x);
    DragTarget HitSlider(float x, float y) const noexcept;
    bool Contains(const D2D1_RECT_F& rectangle, float x, float y) const noexcept;
    D2D1_POINT_2F MousePointInDips(LPARAM lParam) const noexcept;

    void DrawTextLine(const wchar_t* text, const D2D1_RECT_F& rectangle,
                      IDWriteTextFormat* format, D2D1_COLOR_F color);
    void DrawRoundedCard(const D2D1_RECT_F& rectangle, float radius,
                         D2D1_COLOR_F fill, D2D1_COLOR_F border);
    void DrawSlider(const wchar_t* label, const wchar_t* value,
                    const D2D1_RECT_F& track, float normalized);

    static AppWindow* instance_;

    HWND window_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    HINSTANCE module_ = nullptr;
    UINT dpi_ = 96;
    bool hotkeyRegistered_ = false;
    DragTarget dragging_ = DragTarget::None;

    D2D1_RECT_F actionButton_{};
    D2D1_RECT_F radiusTrack_{};
    D2D1_RECT_F deadZoneTrack_{};
    D2D1_RECT_F smoothingTrack_{};
    D2D1_RECT_F blockKeyboardToggle_{};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> headingFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonFormat_;

    InputEngine engine_;
};

