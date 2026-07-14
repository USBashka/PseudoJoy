#include "AppWindow.h"

#include "../res/resource.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace {
constexpr wchar_t kWindowClass[] = L"PseudoJoy.MainWindow";
constexpr wchar_t kWindowTitle[] = L"PseudoJoy";
constexpr wchar_t kSettingsPath[] = L"Software\\PseudoJoy";

const D2D1_COLOR_F Color(const UINT32 rgb, const float alpha = 1.0f) noexcept {
    return D2D1::ColorF(rgb, alpha);
}

const D2D1_COLOR_F kBackground = Color(0x0B111C);
const D2D1_COLOR_F kCard = Color(0x172231, 0.88f);
const D2D1_COLOR_F kCardHover = Color(0x1D2B3D, 0.92f);
const D2D1_COLOR_F kBorder = Color(0x304156, 0.82f);
const D2D1_COLOR_F kText = Color(0xF4F7FC);
const D2D1_COLOR_F kMuted = Color(0x98A8BC);
const D2D1_COLOR_F kAccent = Color(0x42C7F2);
const D2D1_COLOR_F kAccentDeep = Color(0x1978E8);
const D2D1_COLOR_F kSuccess = Color(0x54DEA0);
const D2D1_COLOR_F kDanger = Color(0xFF8E96);

DWORD ReadDword(const wchar_t* name, const DWORD fallback) noexcept {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsPath, name, RRF_RT_REG_DWORD,
                     nullptr, &value, &size) != ERROR_SUCCESS) {
        return fallback;
    }
    return value;
}

void WriteDword(HKEY key, const wchar_t* name, const DWORD value) noexcept {
    RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

float NormalizeValue(const int value, const int minimum, const int maximum) noexcept {
    return static_cast<float>(value - minimum) / static_cast<float>(maximum - minimum);
}

int DenormalizeValue(const float value, const int minimum, const int maximum) noexcept {
    return minimum + static_cast<int>(std::lround(
                         std::clamp(value, 0.0f, 1.0f) * static_cast<float>(maximum - minimum)));
}

}  // namespace

AppWindow* AppWindow::instance_ = nullptr;

AppWindow::AppWindow() {
    instance_ = this;
}

AppWindow::~AppWindow() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

int AppWindow::Run(const HINSTANCE instance, const int showCommand) {
    if (!Create(instance, showCommand)) {
        MessageBoxW(nullptr, L"Не удалось создать окно PseudoJoy.", L"PseudoJoy", MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool AppWindow::Create(const HINSTANCE instance, const int showCommand) {
    module_ = instance;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2dFactory_.ReleaseAndGetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(writeFactory_.ReleaseAndGetAddressOf())))) {
        return false;
    }

    const HICON largeIcon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    const HICON smallIcon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, 0));

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = largeIcon;
    windowClass.hIconSm = smallIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(640, static_cast<int>(dpi_), 96),
                MulDiv(680, static_cast<int>(dpi_), 96)};
    AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                             FALSE, 0, dpi_);

    window_ = CreateWindowExW(
        0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (window_ == nullptr) {
        return false;
    }

    LoadSettings();
    ApplyWindowStyle();
    hotkeyRegistered_ = RegisterHotKey(window_, kHotkeyId, MOD_NOREPEAT, VK_F9) != FALSE;
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProcedure, module_, 0);
    engine_.Start();
    SetTimer(window_, kUiTimer, 33, nullptr);

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

LRESULT CALLBACK AppWindow::WindowProcedure(const HWND window, const UINT message,
                                             const WPARAM wParam, const LPARAM lParam) {
    AppWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    return self != nullptr ? self->HandleMessage(message, wParam, lParam)
                           : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::KeyboardProcedure(const int code, const WPARAM wParam,
                                               const LPARAM lParam) {
    if (code == HC_ACTION && instance_ != nullptr) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool pressed = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool released = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
        if (pressed || released) {
            instance_->engine_.SetKeyState(key->vkCode, pressed);

            if (key->vkCode == VK_F9 && pressed && !instance_->hotkeyRegistered_) {
                PostMessageW(instance_->window_, kFallbackToggleMessage, 0, 0);
            }

            if (instance_->engine_.ShouldBlockKey(key->vkCode)) {
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam) {
    switch (message) {
        case WM_PAINT:
            Paint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            Resize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DiscardGraphicsResources();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = MulDiv(580, static_cast<int>(dpi_), 96);
            limits->ptMinTrackSize.y = MulDiv(700, static_cast<int>(dpi_), 96);
            return 0;
        }

        case WM_HOTKEY:
            if (wParam == kHotkeyId) {
                ToggleCapture();
            }
            return 0;

        case kFallbackToggleMessage:
            ToggleCapture();
            return 0;

        case WM_LBUTTONDOWN: {
            const auto point = MousePointInDips(lParam);
            if (Contains(actionButton_, point.x, point.y)) {
                ToggleCapture();
            } else if (Contains(blockKeyboardToggle_, point.x, point.y)) {
                engine_.SetBlockKeyboard(!engine_.BlockKeyboard());
                SaveSettings();
                InvalidateRect(window_, nullptr, FALSE);
            } else {
                dragging_ = HitSlider(point.x, point.y);
                if (dragging_ != DragTarget::None) {
                    SetCapture(window_);
                    UpdateSlider(dragging_, point.x);
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (dragging_ != DragTarget::None && (wParam & MK_LBUTTON) != 0) {
                UpdateSlider(dragging_, MousePointInDips(lParam).x);
            }
            return 0;

        case WM_LBUTTONUP:
            if (dragging_ != DragTarget::None) {
                UpdateSlider(dragging_, MousePointInDips(lParam).x);
                dragging_ = DragTarget::None;
                ReleaseCapture();
                SaveSettings();
            }
            return 0;

        case WM_TIMER:
            if (wParam == kUiTimer) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;

        case WM_DWMCOMPOSITIONCHANGED:
            ApplyWindowStyle();
            return 0;

        case WM_CLOSE:
            engine_.SetCapturing(false);
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            KillTimer(window_, kUiTimer);
            SaveSettings();
            engine_.SetCapturing(false);
            if (keyboardHook_ != nullptr) {
                UnhookWindowsHookEx(keyboardHook_);
                keyboardHook_ = nullptr;
            }
            if (hotkeyRegistered_) {
                UnregisterHotKey(window_, kHotkeyId);
                hotkeyRegistered_ = false;
            }
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window_, message, wParam, lParam);
    }
}

bool AppWindow::CreateGraphicsResources() {
    if (renderTarget_ != nullptr) {
        return true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(std::max(1L, client.right - client.left)),
        static_cast<UINT32>(std::max(1L, client.bottom - client.top)));

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi_), static_cast<float>(dpi_));

    if (FAILED(d2dFactory_->CreateHwndRenderTarget(
            properties, D2D1::HwndRenderTargetProperties(window_, size),
            renderTarget_.ReleaseAndGetAddressOf())) ||
        FAILED(renderTarget_->CreateSolidColorBrush(kText, brush_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    const auto createFormat = [this](const wchar_t* family, const float size,
                                     const DWRITE_FONT_WEIGHT weight,
                                     Microsoft::WRL::ComPtr<IDWriteTextFormat>& destination) {
        return writeFactory_->CreateTextFormat(
            family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"ru-RU", destination.ReleaseAndGetAddressOf());
    };

    if (FAILED(createFormat(L"Segoe UI Variable Text", 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, headingFormat_)) ||
        FAILED(createFormat(L"Segoe UI Variable Text", 14.0f, DWRITE_FONT_WEIGHT_NORMAL, bodyFormat_)) ||
        FAILED(createFormat(L"Segoe UI Variable Text", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, smallFormat_)) ||
        FAILED(createFormat(L"Segoe UI Variable Text", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, buttonFormat_))) {
        return false;
    }

    for (IDWriteTextFormat* format : {headingFormat_.Get(), bodyFormat_.Get(),
                                     smallFormat_.Get(), buttonFormat_.Get()}) {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return true;
}

void AppWindow::DiscardGraphicsResources() {
    brush_.Reset();
    renderTarget_.Reset();
}

void AppWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!CreateGraphicsResources()) {
        EndPaint(window_, &paint);
        return;
    }

    const D2D1_SIZE_F size = renderTarget_->GetSize();
    const float width = size.width;
    const float margin = 28.0f;
    const InputTelemetry telemetry = engine_.Telemetry();

    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(kBackground);

    const D2D1_RECT_F statusCard = D2D1::RectF(margin, 24.0f, width - margin, 196.0f);
    DrawRoundedCard(statusCard, 18.0f, kCard, kBorder);

    brush_->SetColor(telemetry.capturing ? kSuccess : kMuted);
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(margin + 24.0f, 54.0f), 5.0f, 5.0f), brush_.Get());

    headingFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    const wchar_t* status = telemetry.capturing
                                ? (telemetry.mouseHeld ? L"Передаём движение" : L"Захват запущен")
                                : L"Захват остановлен";
    DrawTextLine(status, D2D1::RectF(margin + 39.0f, 37.0f, width - 170.0f, 70.0f),
                 headingFormat_.Get(), kText);

    std::wstring controllerText;
    if (telemetry.controllerConnected) {
        controllerText = L"Геймпад XInput ";
        controllerText += std::to_wstring(telemetry.controllerIndex + 1);
        controllerText += L" подключён";
    } else {
        controllerText = L"Геймпад не найден · клавиатура доступна";
    }
    DrawTextLine(controllerText.c_str(),
                 D2D1::RectF(margin + 20.0f, 71.0f, width - 160.0f, 95.0f),
                 smallFormat_.Get(), kMuted);

    const D2D1_POINT_2F joystickCenter = D2D1::Point2F(width - 93.0f, 99.0f);
    brush_->SetColor(Color(0x26364A));
    renderTarget_->FillEllipse(D2D1::Ellipse(joystickCenter, 51.0f, 51.0f), brush_.Get());
    brush_->SetColor(kBorder);
    renderTarget_->DrawEllipse(D2D1::Ellipse(joystickCenter, 51.0f, 51.0f), brush_.Get(), 1.0f);
    brush_->SetColor(Color(0x111B29));
    renderTarget_->FillEllipse(D2D1::Ellipse(joystickCenter, 31.0f, 31.0f), brush_.Get());
    const D2D1_POINT_2F knob = D2D1::Point2F(
        joystickCenter.x + telemetry.x * 24.0f,
        joystickCenter.y + telemetry.y * 24.0f);
    brush_->SetColor(telemetry.mouseHeld ? kAccent : Color(0x71839A));
    renderTarget_->FillEllipse(D2D1::Ellipse(knob, 17.0f, 17.0f), brush_.Get());

    actionButton_ = D2D1::RectF(margin + 20.0f, 122.0f, width - 176.0f, 170.0f);
    DrawRoundedCard(actionButton_, 12.0f,
                    telemetry.capturing ? Color(0x3A2630) : Color(0x123A50),
                    telemetry.capturing ? kDanger : kAccent);
    buttonFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextLine(telemetry.capturing ? L"Остановить     F9" : L"Запустить     F9",
                 actionButton_, buttonFormat_.Get(), telemetry.capturing ? kDanger : kText);

    const float settingsBottom = std::max(640.0f, size.height - 47.0f);
    const D2D1_RECT_F settingsCard = D2D1::RectF(margin, 214.0f, width - margin, settingsBottom);
    DrawRoundedCard(settingsCard, 18.0f, kCard, kBorder);
    headingFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawTextLine(L"Настройки", D2D1::RectF(margin + 20.0f, 231.0f, width - margin - 20.0f, 261.0f),
                 headingFormat_.Get(), kText);

    radiusTrack_ = D2D1::RectF(margin + 20.0f, 310.0f, width - margin - 20.0f, 314.0f);
    wchar_t value[32]{};
    swprintf_s(value, L"%d пикс.", engine_.Radius());
    DrawSlider(L"Радиус движения", value, radiusTrack_, NormalizeValue(engine_.Radius(), 16, 300));

    deadZoneTrack_ = D2D1::RectF(margin + 20.0f, 381.0f, width - margin - 20.0f, 385.0f);
    swprintf_s(value, L"%d %%", engine_.DeadZonePercent());
    DrawSlider(L"Мёртвая зона стика", value, deadZoneTrack_,
               NormalizeValue(engine_.DeadZonePercent(), 0, 45));

    smoothingTrack_ = D2D1::RectF(margin + 20.0f, 452.0f, width - margin - 20.0f, 456.0f);
    swprintf_s(value, L"%d мс", engine_.SmoothingMilliseconds());
    DrawSlider(L"Сглаживание стика", value, smoothingTrack_,
               NormalizeValue(engine_.SmoothingMilliseconds(), 0, 250));

    pressDelayTrack_ = D2D1::RectF(margin + 20.0f, 523.0f, width - margin - 20.0f, 527.0f);
    swprintf_s(value, L"%d мс", engine_.PressDelayMilliseconds());
    DrawSlider(L"Пауза перед движением", value, pressDelayTrack_,
               NormalizeValue(engine_.PressDelayMilliseconds(), 0, 120));

    blockKeyboardToggle_ = D2D1::RectF(margin + 20.0f, 557.0f, width - margin - 20.0f, 614.0f);
    DrawRoundedCard(blockKeyboardToggle_, 12.0f, kCardHover, Color(0x35475D));
    bodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawTextLine(L"Блокировать исходные клавиши",
                 D2D1::RectF(blockKeyboardToggle_.left + 15.0f, blockKeyboardToggle_.top + 6.0f,
                             blockKeyboardToggle_.right - 70.0f, blockKeyboardToggle_.top + 29.0f),
                 bodyFormat_.Get(), kText);
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawTextLine(L"WASD и стрелки не попадут в активное приложение",
                 D2D1::RectF(blockKeyboardToggle_.left + 15.0f, blockKeyboardToggle_.top + 27.0f,
                             blockKeyboardToggle_.right - 70.0f, blockKeyboardToggle_.top + 50.0f),
                 smallFormat_.Get(), kMuted);

    const D2D1_RECT_F toggle = D2D1::RectF(blockKeyboardToggle_.right - 57.0f,
                                           blockKeyboardToggle_.top + 16.0f,
                                           blockKeyboardToggle_.right - 13.0f,
                                           blockKeyboardToggle_.top + 40.0f);
    brush_->SetColor(engine_.BlockKeyboard() ? kAccentDeep : Color(0x526176));
    renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(toggle, 12.0f, 12.0f), brush_.Get());
    const float knobX = engine_.BlockKeyboard() ? toggle.right - 12.0f : toggle.left + 12.0f;
    brush_->SetColor(kText);
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, (toggle.top + toggle.bottom) * 0.5f),
                                             8.0f, 8.0f), brush_.Get());

    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    const wchar_t* footer = hotkeyRegistered_
                                ? L"WASD/стрелки · L-стик: джойстик · R-стик: курсор · A/RB: клик · F9"
                                : L"L-стик: джойстик · R-стик: курсор · A/RB: клик · F9 занята";
    DrawTextLine(footer, D2D1::RectF(margin, size.height - 35.0f, width - margin, size.height - 12.0f),
                 smallFormat_.Get(), hotkeyRegistered_ ? kMuted : kDanger);

    const HRESULT result = renderTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardGraphicsResources();
    }
    EndPaint(window_, &paint);
}

void AppWindow::Resize(const UINT width, const UINT height) {
    if (renderTarget_ != nullptr && width > 0 && height > 0) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
}

void AppWindow::ApplyWindowStyle() {
    if (window_ == nullptr) {
        return;
    }

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(window_, 19, &dark, sizeof(dark));

    const int cornerPreference = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(window_, 33, &cornerPreference, sizeof(cornerPreference));

    const COLORREF captionColor = RGB(0x0B, 0x11, 0x1C);
    const COLORREF captionTextColor = RGB(0xF4, 0xF7, 0xFC);
    const COLORREF borderColor = RGB(0x30, 0x41, 0x56);
    DwmSetWindowAttribute(window_, 35, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(window_, 36, &captionTextColor, sizeof(captionTextColor));
    DwmSetWindowAttribute(window_, 34, &borderColor, sizeof(borderColor));

    const int backdropType = 1;  // DWMSBT_NONE
    DwmSetWindowAttribute(window_, 38, &backdropType, sizeof(backdropType));
}

void AppWindow::LoadSettings() {
    engine_.SetRadius(static_cast<int>(ReadDword(L"Radius", 32)));
    engine_.SetDeadZonePercent(static_cast<int>(ReadDword(L"DeadZone", 18)));
    engine_.SetSmoothingMilliseconds(static_cast<int>(ReadDword(L"Smoothing", 65)));
    engine_.SetPressDelayMilliseconds(static_cast<int>(ReadDword(L"PressDelay", 32)));
    engine_.SetBlockKeyboard(ReadDword(L"BlockKeyboard", 1) != 0);
}

void AppWindow::SaveSettings() const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) == ERROR_SUCCESS) {
        WriteDword(key, L"Radius", static_cast<DWORD>(engine_.Radius()));
        WriteDword(key, L"DeadZone", static_cast<DWORD>(engine_.DeadZonePercent()));
        WriteDword(key, L"Smoothing", static_cast<DWORD>(engine_.SmoothingMilliseconds()));
        WriteDword(key, L"PressDelay", static_cast<DWORD>(engine_.PressDelayMilliseconds()));
        WriteDword(key, L"BlockKeyboard", engine_.BlockKeyboard() ? 1U : 0U);
        RegCloseKey(key);
    }
}

void AppWindow::ToggleCapture() {
    engine_.ToggleCapturing();
    InvalidateRect(window_, nullptr, FALSE);
}

void AppWindow::UpdateSlider(const DragTarget target, const float x) {
    const D2D1_RECT_F* track = nullptr;
    switch (target) {
        case DragTarget::Radius: track = &radiusTrack_; break;
        case DragTarget::DeadZone: track = &deadZoneTrack_; break;
        case DragTarget::Smoothing: track = &smoothingTrack_; break;
        case DragTarget::PressDelay: track = &pressDelayTrack_; break;
        case DragTarget::None: return;
    }

    const float normalized = (x - track->left) / (track->right - track->left);
    switch (target) {
        case DragTarget::Radius:
            engine_.SetRadius(DenormalizeValue(normalized, 16, 300));
            break;
        case DragTarget::DeadZone:
            engine_.SetDeadZonePercent(DenormalizeValue(normalized, 0, 45));
            break;
        case DragTarget::Smoothing:
            engine_.SetSmoothingMilliseconds(DenormalizeValue(normalized, 0, 250));
            break;
        case DragTarget::PressDelay:
            engine_.SetPressDelayMilliseconds(DenormalizeValue(normalized, 0, 120));
            break;
        case DragTarget::None:
            break;
    }
    InvalidateRect(window_, nullptr, FALSE);
}

AppWindow::DragTarget AppWindow::HitSlider(const float x, const float y) const noexcept {
    const auto hit = [x, y](const D2D1_RECT_F& track) {
        return x >= track.left - 8.0f && x <= track.right + 8.0f &&
               y >= track.top - 18.0f && y <= track.bottom + 18.0f;
    };
    if (hit(radiusTrack_)) return DragTarget::Radius;
    if (hit(deadZoneTrack_)) return DragTarget::DeadZone;
    if (hit(smoothingTrack_)) return DragTarget::Smoothing;
    if (hit(pressDelayTrack_)) return DragTarget::PressDelay;
    return DragTarget::None;
}

bool AppWindow::Contains(const D2D1_RECT_F& rectangle, const float x, const float y) const noexcept {
    return x >= rectangle.left && x <= rectangle.right && y >= rectangle.top && y <= rectangle.bottom;
}

D2D1_POINT_2F AppWindow::MousePointInDips(const LPARAM lParam) const noexcept {
    const float scale = 96.0f / static_cast<float>(std::max(1U, dpi_));
    return D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)) * scale,
                         static_cast<float>(GET_Y_LPARAM(lParam)) * scale);
}

void AppWindow::DrawTextLine(const wchar_t* text, const D2D1_RECT_F& rectangle,
                             IDWriteTextFormat* format, const D2D1_COLOR_F color) {
    brush_->SetColor(color);
    renderTarget_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format, rectangle,
                             brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void AppWindow::DrawRoundedCard(const D2D1_RECT_F& rectangle, const float radius,
                                const D2D1_COLOR_F fill, const D2D1_COLOR_F border) {
    const auto rounded = D2D1::RoundedRect(rectangle, radius, radius);
    brush_->SetColor(fill);
    renderTarget_->FillRoundedRectangle(rounded, brush_.Get());
    brush_->SetColor(border);
    renderTarget_->DrawRoundedRectangle(rounded, brush_.Get(), 1.0f);
}

void AppWindow::DrawSlider(const wchar_t* label, const wchar_t* value,
                           const D2D1_RECT_F& track, const float normalized) {
    bodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawTextLine(label, D2D1::RectF(track.left, track.top - 35.0f, track.right - 95.0f, track.top - 10.0f),
                 bodyFormat_.Get(), kText);
    bodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    DrawTextLine(value, D2D1::RectF(track.right - 95.0f, track.top - 35.0f, track.right, track.top - 10.0f),
                 bodyFormat_.Get(), kMuted);

    brush_->SetColor(Color(0x405168));
    renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(track, 2.0f, 2.0f), brush_.Get());
    const float knobX = track.left + std::clamp(normalized, 0.0f, 1.0f) * (track.right - track.left);
    const D2D1_RECT_F fill = D2D1::RectF(track.left, track.top, knobX, track.bottom);
    brush_->SetColor(kAccentDeep);
    renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(fill, 2.0f, 2.0f), brush_.Get());
    brush_->SetColor(kAccent);
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, (track.top + track.bottom) * 0.5f),
                                             7.0f, 7.0f), brush_.Get());
}
