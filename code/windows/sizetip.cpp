/*
 * sizetip.cpp - Direct2D/DirectWrite resize tips for PuTTY(tel) terminal window.
 */

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>

extern "C" {
extern HINSTANCE hinst;
void UpdateSizeTip(HWND src, int cx, int cy);
void EnableSizeTip(bool bEnable);
void ShowFontTip(HWND anchor, const char *text, int duration_ms);
void HideFontTip(void);
}

static ATOM tip_class = 0;
static HWND tip_wnd = NULL;
static bool tip_enabled = false;

static COLORREF tip_bg = RGB(255, 255, 225);
static COLORREF tip_text = RGB(0, 0, 0);

static ID2D1Factory *tip_d2dF = NULL;
static IDWriteFactory *tip_dwF = NULL;
static IDWriteTextFormat *tip_format = NULL;
static ID2D1HwndRenderTarget *tip_rt = NULL;
static ID2D1SolidColorBrush *tip_bg_brush = NULL;
static ID2D1SolidColorBrush *tip_fg_brush = NULL;
static ID2D1SolidColorBrush *tip_border_brush = NULL;

static D2D1_COLOR_F tip_color_from_ref(COLORREF c)
{
    return D2D1::ColorF(
        GetRValue(c) / 255.0f,
        GetGValue(c) / 255.0f,
        GetBValue(c) / 255.0f,
        1.0f);
}

static void tip_release_rt(void)
{
    if (tip_border_brush) {
        tip_border_brush->Release();
        tip_border_brush = NULL;
    }
    if (tip_fg_brush) {
        tip_fg_brush->Release();
        tip_fg_brush = NULL;
    }
    if (tip_bg_brush) {
        tip_bg_brush->Release();
        tip_bg_brush = NULL;
    }
    if (tip_rt) {
        tip_rt->Release();
        tip_rt = NULL;
    }
}

static void tip_release_all(void)
{
    tip_release_rt();
    if (tip_format) {
        tip_format->Release();
        tip_format = NULL;
    }
    if (tip_dwF) {
        tip_dwF->Release();
        tip_dwF = NULL;
    }
    if (tip_d2dF) {
        tip_d2dF->Release();
        tip_d2dF = NULL;
    }
}

static void tip_ensure_resources(void)
{
    if (!tip_d2dF) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                          __uuidof(ID2D1Factory), NULL,
                          reinterpret_cast<void **>(&tip_d2dF));
    }
    if (!tip_dwF) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown **>(&tip_dwF));
    }
    if (tip_dwF && !tip_format) {
        NONCLIENTMETRICSW nci;
        ZeroMemory(&nci, sizeof(nci));
        nci.cbSize = sizeof(nci);
        const WCHAR *fontName = L"Segoe UI";
        FLOAT fontSize = 12.0f;
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
                                  sizeof(nci), &nci, 0)) {
            if (nci.lfStatusFont.lfFaceName[0] != L'\0')
                fontName = nci.lfStatusFont.lfFaceName;
            if (nci.lfStatusFont.lfHeight != 0)
                fontSize = (FLOAT)abs(nci.lfStatusFont.lfHeight);
        }
        tip_dwF->CreateTextFormat(
            fontName, NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &tip_format);
    }
}

static void tip_measure_text(const WCHAR *str, int *out_w, int *out_h)
{
    int w = 40;
    int h = 18;
    tip_ensure_resources();
    if (tip_dwF && tip_format && str) {
        IDWriteTextLayout *layout = NULL;
        UINT32 len = (UINT32)wcslen(str);
        if (SUCCEEDED(tip_dwF->CreateTextLayout(
                str, len, tip_format, 4096.0f, 4096.0f, &layout)) && layout) {
            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(layout->GetMetrics(&tm))) {
                w = (int)ceilf(tm.width) + 6;
                h = (int)ceilf(tm.height) + 6;
            }
            layout->Release();
        }
    }
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    *out_w = w;
    *out_h = h;
}

static LRESULT CALLBACK SizeTipWndProc(HWND hWnd, UINT nMsg,
                                       WPARAM wParam, LPARAM lParam)
{
    switch (nMsg) {
      case WM_ERASEBKGND:
        return TRUE;

      case WM_PAINT: {
        RECT cr;
        GetClientRect(hWnd, &cr);
        UINT32 cw = (UINT32)(cr.right - cr.left);
        UINT32 ch = (UINT32)(cr.bottom - cr.top);

        tip_ensure_resources();
        if (tip_d2dF && !tip_rt && cw > 0 && ch > 0) {
            D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
            D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp =
                D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(cw, ch));
            if (SUCCEEDED(tip_d2dF->CreateHwndRenderTarget(rtp, hrtp, &tip_rt)) &&
                tip_rt) {
                tip_rt->SetDpi(96.0f, 96.0f);
                tip_rt->CreateSolidColorBrush(
                    tip_color_from_ref(tip_bg), &tip_bg_brush);
                tip_rt->CreateSolidColorBrush(
                    tip_color_from_ref(tip_text), &tip_fg_brush);
                tip_rt->CreateSolidColorBrush(
                    D2D1::ColorF(D2D1::ColorF::Black), &tip_border_brush);
            }
        }

        if (tip_rt) {
            int wtlen = GetWindowTextLengthW(hWnd);
            WCHAR *wt = new WCHAR[wtlen + 1];
            GetWindowTextW(hWnd, wt, wtlen + 1);

            tip_rt->BeginDraw();
            tip_rt->Clear(tip_color_from_ref(tip_bg));
            if (tip_border_brush) {
                D2D1_RECT_F border = D2D1::RectF(
                    0.5f, 0.5f, (FLOAT)cw - 0.5f, (FLOAT)ch - 0.5f);
                tip_rt->DrawRectangle(border, tip_border_brush, 1.0f);
            }
            if (tip_format && tip_fg_brush && wtlen > 0) {
                D2D1_RECT_F layoutRect = D2D1::RectF(
                    3.0f, 3.0f, (FLOAT)cw, (FLOAT)ch);
                tip_rt->DrawText(
                    wt, (UINT32)wtlen, tip_format, layoutRect, tip_fg_brush);
            }
            if (tip_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                tip_release_rt();
            }

            delete[] wt;
        }

        ValidateRect(hWnd, NULL);
        return 0;
      }

      case WM_NCHITTEST:
        return HTTRANSPARENT;

      case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hWnd, 1);
            DestroyWindow(hWnd);
            if (tip_wnd == hWnd)
                tip_wnd = NULL;
            return 0;
        }
        break;

      case WM_DESTROY:
        tip_release_all();
        break;

      case WM_SETTEXT: {
        LPCSTR str = (LPCSTR)lParam;
        WCHAR wstr[64];
        int w = 0, h = 0;
        MultiByteToWideChar(CP_ACP, 0, str ? str : "", -1,
                            wstr, ARRAYSIZE(wstr));
        tip_measure_text(wstr, &w, &h);
        SetWindowPos(hWnd, NULL, 0, 0, w, h,
                     SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
        if (tip_rt) {
            tip_rt->Resize(D2D1::SizeU((UINT32)w, (UINT32)h));
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;
      }
    }

    return DefWindowProc(hWnd, nMsg, wParam, lParam);
}

extern "C" void UpdateSizeTip(HWND src, int cx, int cy)
{
    char str[32];

    if (!tip_enabled)
        return;

    if (!tip_wnd) {
        if (!tip_class) {
            WNDCLASSA wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = SizeTipWndProc;
            wc.hInstance = hinst;
            wc.lpszClassName = "SizeTipClass";
            tip_class = RegisterClassA(&wc);
        }

        tip_bg = GetSysColor(COLOR_INFOBK);
        tip_text = GetSysColor(COLOR_INFOTEXT);
        tip_ensure_resources();
    }

    sprintf(str, "%dx%d", cx, cy);

    if (!tip_wnd) {
        WCHAR wstr[32];
        int w = 0, h = 0;
        RECT wr;
        int ix, iy;

        MultiByteToWideChar(CP_ACP, 0, str, -1, wstr, ARRAYSIZE(wstr));
        tip_measure_text(wstr, &w, &h);

        GetWindowRect(src, &wr);
        ix = wr.left;
        if (ix < 16)
            ix = 16;

        iy = wr.top - h;
        if (iy < 16)
            iy = 16;

        tip_wnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            MAKEINTRESOURCEA(tip_class), str, WS_POPUP, ix, iy, w, h,
            NULL, NULL, hinst, NULL);

        ShowWindow(tip_wnd, SW_SHOWNOACTIVATE);
    } else {
        SetWindowTextA(tip_wnd, str);
    }
}

extern "C" void EnableSizeTip(bool bEnable)
{
    if (tip_wnd && !bEnable) {
        DestroyWindow(tip_wnd);
        tip_wnd = NULL;
    }

    tip_enabled = bEnable;
}

static void tip_ensure_class_and_res(void)
{
    if (!tip_class) {
        WNDCLASSA wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SizeTipWndProc;
        wc.hInstance = hinst;
        wc.lpszClassName = "SizeTipClass";
        tip_class = RegisterClassA(&wc);
    }

    tip_bg = GetSysColor(COLOR_INFOBK);
    tip_text = GetSysColor(COLOR_INFOTEXT);
    tip_ensure_resources();
}

extern "C" void ShowFontTip(HWND anchor, const char *text, int duration_ms)
{
    if (!text || !*text)
        return;

    if (!tip_wnd)
        tip_ensure_class_and_res();

    WCHAR wstr[64];
    int w = 0, h = 0;
    MultiByteToWideChar(CP_ACP, 0, text, -1, wstr, ARRAYSIZE(wstr));
    tip_measure_text(wstr, &w, &h);

    RECT ar = {0, 0, 0, 0};
    if (anchor && IsWindow(anchor))
        GetWindowRect(anchor, &ar);

    int ix = ar.left;
    int iy = ar.bottom + 4;
    HMONITOR hmon = MonitorFromRect(&ar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        if (ix + w > mi.rcWork.right - 4)
            ix = mi.rcWork.right - w - 4;
        if (ix < mi.rcWork.left + 4)
            ix = mi.rcWork.left + 4;
        if (iy + h > mi.rcWork.bottom - 4)
            iy = ar.top - h - 4;
        if (iy < mi.rcWork.top + 4)
            iy = mi.rcWork.top + 4;
    }

    if (!tip_wnd) {
        tip_wnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            MAKEINTRESOURCEA(tip_class), text, WS_POPUP, ix, iy, w, h,
            NULL, NULL, hinst, NULL);
        ShowWindow(tip_wnd, SW_SHOWNOACTIVATE);
    } else {
        SetWindowTextA(tip_wnd, text);
        SetWindowPos(tip_wnd, HWND_TOPMOST, ix, iy, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    if (tip_wnd) {
        if (duration_ms > 0)
            SetTimer(tip_wnd, 1, (UINT)duration_ms, NULL);
        else
            KillTimer(tip_wnd, 1);
    }
}

extern "C" void HideFontTip(void)
{
    if (tip_wnd) {
        KillTimer(tip_wnd, 1);
        DestroyWindow(tip_wnd);
        tip_wnd = NULL;
    }
}
