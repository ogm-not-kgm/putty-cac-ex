#include "fontzoom.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windowsx.h>

extern "C" HINSTANCE hinst;

namespace {

constexpr int kMinFontPt = 4;
constexpr int kMaxFontPt = 72;
constexpr int kTipDurationMs = 1200;
constexpr UINT_PTR kTimerCtrlRelease = 1;
constexpr int kCtrlPollMs = 30;
constexpr int kCtrlEditId = 101;
constexpr int kCtrlUpDownId = 102;
constexpr int kCtrlPickBtnId = 103;

ATOM g_host_class = 0;
ATOM g_preview_class = 0;
ATOM g_pick_class = 0;

int clamp_pt(int pt) {
    if (pt < kMinFontPt) return kMinFontPt;
    if (pt > kMaxFontPt) return kMaxFontPt;
    return pt;
}

UINT query_window_dpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    static GetDpiForWindowFn pGetDpiForWindow = []() -> GetDpiForWindowFn {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        return u32 ? reinterpret_cast<GetDpiForWindowFn>(
                         GetProcAddress(u32, "GetDpiForWindow"))
                   : nullptr;
    }();
    if (pGetDpiForWindow && hwnd) {
        UINT dpi = pGetDpiForWindow(hwnd);
        if (dpi > 0) return dpi;
    }
    return 96;
}

HFONT create_ui_font(UINT dpi) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    const WCHAR *face = L"Segoe UI";
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        if (ncm.lfCaptionFont.lfFaceName[0] != L'\0')
            face = ncm.lfCaptionFont.lfFaceName;
    }
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(9, static_cast<int>(dpi), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

int parse_pt_from_edit(HWND edit, int fallback_pt) {
    WCHAR buf[64] = {};
    GetWindowTextW(edit, buf, ARRAYSIZE(buf));
    WCHAR *endp = nullptr;
    long val = wcstol(buf, &endp, 10);
    if (endp == buf)
        return fallback_pt;
    return clamp_pt(static_cast<int>(val));
}

} // namespace

struct FontZoomState {
    HWND term_hwnd = nullptr;
    HWND host_hwnd = nullptr;
    HWND pick_btn_hwnd = nullptr;
    HWND edit_hwnd = nullptr;
    HWND updown_hwnd = nullptr;
    HWND preview_hwnd = nullptr;
    HFONT ui_font = nullptr;
    WNDPROC orig_edit_proc = nullptr;
    WNDPROC orig_updown_proc = nullptr;
    UINT current_dpi = 96;

    FontZoomCallbacks cb = {};
    void *ctx = nullptr;

    bool updating_text = false;
    bool preview_active = false;
    int preview_pt = 10;
    int wheel_accum = 0;

    bool pick_dragging = false;
    bool pick_hover = false;
    HWND pick_target_hwnd = nullptr;
};

static void set_edit_pt_text(FontZoomState *fz, int pt) {
    if (!fz || !fz->edit_hwnd)
        return;
    WCHAR buf[32];
    swprintf_s(buf, L"%d pt", pt);
    fz->updating_text = true;
    SetWindowTextW(fz->edit_hwnd, buf);
    fz->updating_text = false;
}

static void show_pt_tip(FontZoomState *fz, int pt, int duration_ms,
                        bool with_px, int px_w, int px_h) {
    if (!fz || !fz->cb.show_tip)
        return;
    HWND anchor = (fz->host_hwnd && IsWindowVisible(fz->host_hwnd))
                      ? fz->host_hwnd
                      : fz->term_hwnd;
    char buf[64];
    if (with_px) {
        sprintf_s(buf, "%d pt (%dx%d)", pt, px_w, px_h);
    } else {
        sprintf_s(buf, "%d pt", pt);
    }
    fz->cb.show_tip(anchor, buf, duration_ms);
}

static void update_preview_frame(FontZoomState *fz, const RECT &rc) {
    if (!fz || !fz->preview_hwnd)
        return;

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 4) w = 4;
    if (h < 4) h = 4;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ old_bmp = SelectObject(mem_dc, dib);

        // Premultiplied BGRA pixels:
        // Border (2px): A=230, R=0, G=120, B=215 -> (0, 108, 194, 230)
        // Fill:         A=32,  R=0, G=120, B=215 -> (0, 15,  27,  32)
        const DWORD border_px = (230u << 24) | (0u << 16) | (108u << 8) | 194u;
        const DWORD fill_px = (32u << 24) | (0u << 16) | (15u << 8) | 27u;
        const int bw = 2;

        DWORD *px = static_cast<DWORD *>(bits);
        for (int y = 0; y < h; ++y) {
            bool is_y_border = (y < bw || y >= h - bw);
            DWORD *row = px + y * w;
            if (is_y_border) {
                for (int x = 0; x < w; ++x)
                    row[x] = border_px;
            } else {
                for (int x = 0; x < w; ++x) {
                    row[x] = (x < bw || x >= w - bw) ? border_px : fill_px;
                }
            }
        }

        POINT pt_dst = {rc.left, rc.top};
        SIZE sz = {w, h};
        POINT pt_src = {0, 0};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(fz->preview_hwnd, screen_dc, &pt_dst, &sz,
                            mem_dc, &pt_src, 0, &bf, ULW_ALPHA);
        ShowWindow(fz->preview_hwnd, SW_SHOWNOACTIVATE);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(dib);
    }
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
}

static void cancel_preview(FontZoomState *fz) {
    if (!fz || !fz->preview_active)
        return;
    fz->preview_active = false;
    fz->wheel_accum = 0;
    if (fz->host_hwnd)
        KillTimer(fz->host_hwnd, kTimerCtrlRelease);
    if (fz->preview_hwnd)
        ShowWindow(fz->preview_hwnd, SW_HIDE);
    if (fz->cb.hide_tip)
        fz->cb.hide_tip();
    fontzoom_sync_text(fz);
}

static void apply_pt_immediate(FontZoomState *fz, int target_pt, bool show_tip) {
    if (!fz || !fz->term_hwnd || IsZoomed(fz->term_hwnd))
        return;
    bool had_focus = (fz->edit_hwnd && GetFocus() == fz->edit_hwnd);
    int pt = clamp_pt(target_pt);
    if (fz->cb.apply_pt)
        fz->cb.apply_pt(fz->ctx, pt);
    set_edit_pt_text(fz, pt);
    if (had_focus && fz->edit_hwnd && GetFocus() != fz->edit_hwnd)
        SetFocus(fz->edit_hwnd);
    if (show_tip)
        show_pt_tip(fz, pt, kTipDurationMs, false, 0, 0);
}

static void commit_preview(FontZoomState *fz) {
    if (!fz || !fz->preview_active)
        return;
    int target_pt = fz->preview_pt;
    fz->preview_active = false;
    fz->wheel_accum = 0;
    if (fz->host_hwnd)
        KillTimer(fz->host_hwnd, kTimerCtrlRelease);
    if (fz->preview_hwnd)
        ShowWindow(fz->preview_hwnd, SW_HIDE);

    if (!IsZoomed(fz->term_hwnd) && fz->cb.apply_pt) {
        fz->cb.apply_pt(fz->ctx, target_pt);
    }
    fontzoom_sync_text(fz);
    show_pt_tip(fz, target_pt, kTipDurationMs, false, 0, 0);
}

static void on_ctrl_wheel(FontZoomState *fz, short delta) {
    if (!fz || !fz->term_hwnd || IsZoomed(fz->term_hwnd))
        return;

    fz->wheel_accum += delta;
    int steps = fz->wheel_accum / WHEEL_DELTA;
    if (steps == 0 && fz->preview_active)
        return;

    if (!fz->preview_active) {
        int cur_pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
        fz->preview_pt = clamp_pt(cur_pt);
        fz->preview_active = true;
        if (fz->host_hwnd)
            SetTimer(fz->host_hwnd, kTimerCtrlRelease, kCtrlPollMs, nullptr);
    }

    if (steps != 0) {
        fz->wheel_accum -= steps * WHEEL_DELTA;
        fz->preview_pt = clamp_pt(fz->preview_pt + steps);
    }

    set_edit_pt_text(fz, fz->preview_pt);

    int cw = 0, ch = 0;
    int cols = 0, rows = 0, extra_w = 0, extra_h = 0;
    if (fz->cb.get_term_grid)
        fz->cb.get_term_grid(fz->ctx, &cols, &rows, &extra_w, &extra_h);

    if (cols > 0 && rows > 0 && fz->cb.measure_cell_for_pt &&
        fz->cb.measure_cell_for_pt(fz->ctx, fz->preview_pt, &cw, &ch)) {
        RECT wr = {};
        GetWindowRect(fz->term_hwnd, &wr);
        int pred_w = cw * cols + extra_w;
        int pred_h = ch * rows + extra_h;
        RECT pred_rc = {wr.left, wr.top, wr.left + pred_w, wr.top + pred_h};
        update_preview_frame(fz, pred_rc);
        show_pt_tip(fz, fz->preview_pt, 0, true, pred_w, pred_h);
    } else {
        show_pt_tip(fz, fz->preview_pt, 0, false, 0, 0);
    }
}

static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto *fz = reinterpret_cast<FontZoomState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!fz)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
      case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;

      case WM_LBUTTONDOWN:
        break;

      case WM_MOUSEWHEEL: {
        bool ctrl = (LOWORD(wParam) & MK_CONTROL) != 0 ||
                    (GetKeyState(VK_CONTROL) < 0);
        if (ctrl) {
            on_ctrl_wheel(fz, static_cast<short>(HIWORD(wParam)));
            return 0;
        }
        break;
      }

      case WM_KEYDOWN:
      case WM_SYSKEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) < 0) && (GetKeyState(VK_MENU) >= 0);
        if (ctrl) {
            int cur_pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
                apply_pt_immediate(fz, cur_pt + 1, true);
                return 0;
            }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
                apply_pt_immediate(fz, cur_pt - 1, true);
                return 0;
            }
            if (wParam == '0' || wParam == VK_NUMPAD0) {
                int base_pt = fz->cb.get_base_pt ? fz->cb.get_base_pt(fz->ctx) : 10;
                apply_pt_immediate(fz, base_pt, true);
                return 0;
            }
        }
        if (wParam == VK_RETURN) {
            int cur_pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
            int target_pt = parse_pt_from_edit(hwnd, cur_pt);
            apply_pt_immediate(fz, target_pt, true);
            SetFocus(fz->term_hwnd);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            cancel_preview(fz);
            fontzoom_sync_text(fz);
            SetFocus(fz->term_hwnd);
            return 0;
        }
        if (wParam == VK_UP) {
            int cur_pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
            apply_pt_immediate(fz, cur_pt + 1, true);
            return 0;
        }
        if (wParam == VK_DOWN) {
            int cur_pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
            apply_pt_immediate(fz, cur_pt - 1, true);
            return 0;
        }
        break;
      }

      case WM_KEYUP:
        if (wParam == VK_CONTROL && fz->preview_active) {
            commit_preview(fz);
            return 0;
        }
        break;

      case WM_CHAR:
        if (wParam == L'\r' || wParam == L'\n' || wParam == 0x1B)
            return 0;
        break;

      case WM_KILLFOCUS:
        if (!fz->preview_active)
            fontzoom_sync_text(fz);
        break;
    }

    return CallWindowProcW(fz->orig_edit_proc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK updown_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto *fz = reinterpret_cast<FontZoomState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!fz)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (msg == WM_MOUSEWHEEL) {
        bool ctrl = (LOWORD(wParam) & MK_CONTROL) != 0 ||
                    (GetKeyState(VK_CONTROL) < 0);
        if (ctrl) {
            on_ctrl_wheel(fz, static_cast<short>(HIWORD(wParam)));
            return 0;
        }
    }
    return CallWindowProcW(fz->orig_updown_proc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK host_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto *fz = reinterpret_cast<FontZoomState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
      case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;

      case WM_MOUSEWHEEL:
        if (fz) {
            bool ctrl = (LOWORD(wParam) & MK_CONTROL) != 0 ||
                        (GetKeyState(VK_CONTROL) < 0);
            if (ctrl) {
                on_ctrl_wheel(fz, static_cast<short>(HIWORD(wParam)));
                return 0;
            }
        }
        break;

      case WM_TIMER:
        if (fz && wParam == kTimerCtrlRelease && fz->preview_active) {
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0) {
                commit_preview(fz);
            }
            return 0;
        }
        break;

      case WM_NOTIFY:
        if (fz) {
            auto *nm = reinterpret_cast<NMHDR *>(lParam);
            if (nm && nm->hwndFrom == fz->updown_hwnd && nm->code == UDN_DELTAPOS) {
                if (IsZoomed(fz->term_hwnd))
                    return TRUE;
                auto *ud = reinterpret_cast<NMUPDOWN *>(lParam);
                int cur_pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
                // UpDown default has iDelta > 0 for up or down depending on range;
                // range is set 4..72 so up arrow gives iDelta = +1.
                apply_pt_immediate(fz, cur_pt + ud->iDelta, true);
                return TRUE;
            }
        }
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void cancel_pick_drag(FontZoomState *fz) {
    if (!fz || !fz->pick_dragging)
        return;
    fz->pick_dragging = false;
    fz->pick_target_hwnd = nullptr;
    if (GetCapture() == fz->pick_btn_hwnd)
        ReleaseCapture();
    if (fz->preview_hwnd)
        ShowWindow(fz->preview_hwnd, SW_HIDE);
    if (fz->pick_btn_hwnd)
        InvalidateRect(fz->pick_btn_hwnd, nullptr, FALSE);
}

static void update_pick_target_at_cursor(FontZoomState *fz) {
    if (!fz || !fz->pick_dragging)
        return;
    POINT pt = {};
    GetCursorPos(&pt);
    HWND hit = WindowFromPoint(pt);
    HWND root = hit ? GetAncestor(hit, GA_ROOT) : nullptr;
    bool valid = false;
    if (root && root != fz->term_hwnd && root != fz->host_hwnd &&
        root != fz->preview_hwnd && IsWindowVisible(root) && !IsIconic(root)) {
        if (!fz->cb.can_pick_window || fz->cb.can_pick_window(fz->ctx, root))
            valid = true;
    }

    if (valid) {
        if (root != fz->pick_target_hwnd) {
            fz->pick_target_hwnd = root;
            RECT trc = {};
            if (GetWindowRect(root, &trc))
                update_preview_frame(fz, trc);
        }
    } else {
        fz->pick_target_hwnd = nullptr;
        if (fz->preview_hwnd)
            ShowWindow(fz->preview_hwnd, SW_HIDE);
    }
}

static void paint_pick_button(FontZoomState *fz, HWND hwnd, HDC hdc) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return;

    bool enabled = IsWindowEnabled(hwnd) != FALSE;
    COLORREF bg = GetSysColor(COLOR_WINDOW);
    if (!enabled) {
        bg = GetSysColor(COLOR_BTNFACE);
    } else if (fz && fz->pick_dragging) {
        bg = RGB(204, 228, 247);
    } else if (fz && fz->pick_hover) {
        bg = RGB(229, 241, 251);
    }

    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(hdc, &rc, bg_brush);
    DeleteObject(bg_brush);

    COLORREF border_col = (fz && fz->pick_dragging) ? RGB(0, 120, 215)
                          : (fz && fz->pick_hover)  ? RGB(0, 120, 215)
                                                    : GetSysColor(COLOR_WINDOWFRAME);
    HBRUSH border_brush = CreateSolidBrush(border_col);
    FrameRect(hdc, &rc, border_brush);
    DeleteObject(border_brush);

    int pad = (w < h ? w : h) / 5;
    if (pad < 3) pad = 3;
    int x0 = rc.left + pad;
    int y0 = rc.top + pad;
    int x1 = rc.right - pad - 1;
    int y1 = rc.bottom - pad - 1;
    int span = ((x1 - x0) < (y1 - y0)) ? (x1 - x0) : (y1 - y0);
    if (span < 6)
        return;
    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;
    int half = span / 2;
    int left = cx - half;
    int top = cy - half;
    int right = cx + half;
    int bottom = cy + half;

    COLORREF fg = enabled ? GetSysColor(COLOR_WINDOWTEXT) : GetSysColor(COLOR_GRAYTEXT);
    int pen_w = (span >= 14) ? 2 : 1;
    HPEN pen = CreatePen(PS_SOLID, pen_w, fg);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // Eyedropper shaft from bottom-left tip to upper-right bulb
    int tip_x = left + 1;
    int tip_y = bottom - 1;
    int neck_x = cx + half / 4;
    int neck_y = cy - half / 4;
    MoveToEx(hdc, tip_x, tip_y, nullptr);
    LineTo(hdc, neck_x, neck_y);

    // Collar perpendicular to shaft
    int col_dx = span / 5;
    if (col_dx < 2) col_dx = 2;
    MoveToEx(hdc, neck_x - col_dx, neck_y - col_dx, nullptr);
    LineTo(hdc, neck_x + col_dx, neck_y + col_dx);

    // Bulb at top-right
    int bulb_r = span / 5;
    if (bulb_r < 2) bulb_r = 2;
    int bulb_cx = right - bulb_r;
    int bulb_cy = top + bulb_r;
    HBRUSH bulb_brush = CreateSolidBrush(fg);
    SelectObject(hdc, bulb_brush);
    Ellipse(hdc, bulb_cx - bulb_r, bulb_cy - bulb_r, bulb_cx + bulb_r + 1, bulb_cy + bulb_r + 1);
    SelectObject(hdc, old_brush);
    DeleteObject(bulb_brush);

    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static LRESULT CALLBACK pick_btn_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto *fz = reinterpret_cast<FontZoomState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_pick_button(fz, hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_SETCURSOR:
        if (fz && fz->pick_dragging) {
            SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_CROSS)));
            return TRUE;
        }
        break;
      case WM_MOUSEMOVE:
        if (fz) {
            if (fz->pick_dragging) {
                update_pick_target_at_cursor(fz);
                return 0;
            }
            if (!fz->pick_hover) {
                fz->pick_hover = true;
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;
      case WM_MOUSELEAVE:
        if (fz && fz->pick_hover) {
            fz->pick_hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
      case WM_LBUTTONDOWN:
        if (fz && IsWindowEnabled(hwnd)) {
            cancel_preview(fz);
            fz->pick_dragging = true;
            fz->pick_target_hwnd = nullptr;
            SetCapture(hwnd);
            SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_CROSS)));
            InvalidateRect(hwnd, nullptr, FALSE);
            update_pick_target_at_cursor(fz);
            return 0;
        }
        break;
      case WM_LBUTTONUP:
        if (fz && fz->pick_dragging) {
            HWND target = fz->pick_target_hwnd;
            cancel_pick_drag(fz);
            if (target && fz->cb.pick_font_from_window)
                fz->cb.pick_font_from_window(fz->ctx, target);
            return 0;
        }
        break;
      case WM_KEYDOWN:
        if (fz && fz->pick_dragging && wParam == VK_ESCAPE) {
            cancel_pick_drag(fz);
            return 0;
        }
        break;
      case WM_CAPTURECHANGED:
        if (fz && fz->pick_dragging && reinterpret_cast<HWND>(lParam) != hwnd) {
            fz->pick_dragging = false;
            fz->pick_target_hwnd = nullptr;
            if (fz->preview_hwnd)
                ShowWindow(fz->preview_hwnd, SW_HIDE);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK preview_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensure_classes_registered() {
    if (!g_host_class) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = host_wnd_proc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"PuTTYFontZoomHost";
        g_host_class = RegisterClassExW(&wc);
    }
    if (!g_preview_class) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = preview_wnd_proc;
        wc.hInstance = hinst;
        wc.lpszClassName = L"PuTTYFontZoomPreview";
        g_preview_class = RegisterClassExW(&wc);
    }
    if (!g_pick_class) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = pick_btn_proc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_HAND));
        wc.lpszClassName = L"PuTTYFontPickBtn";
        g_pick_class = RegisterClassExW(&wc);
    }
}

static void ensure_updown_common_controls() {
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;
    HMODULE mod = LoadLibraryW(L"comctl32.dll");
    if (!mod)
        return;
    using InitCCExFn = BOOL(WINAPI *)(const INITCOMMONCONTROLSEX *);
    auto pInit = reinterpret_cast<InitCCExFn>(
        GetProcAddress(mod, "InitCommonControlsEx"));
    if (pInit) {
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_UPDOWN_CLASS};
        pInit(&icc);
    }
}

FontZoomState *fontzoom_create(HWND term_hwnd, const FontZoomCallbacks *cb, void *ctx) {
    if (!term_hwnd || !cb)
        return nullptr;

    ensure_updown_common_controls();
    ensure_classes_registered();

    auto *fz = new FontZoomState();
    fz->term_hwnd = term_hwnd;
    fz->cb = *cb;
    fz->ctx = ctx;
    fz->current_dpi = query_window_dpi(term_hwnd);
    fz->ui_font = create_ui_font(fz->current_dpi);

    fz->host_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        MAKEINTRESOURCEW(g_host_class), L"",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 94, 24,
        term_hwnd, nullptr, hinst, nullptr);
    if (!fz->host_hwnd) {
        fontzoom_destroy(fz);
        return nullptr;
    }
    SetWindowLongPtrW(fz->host_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fz));

    fz->pick_btn_hwnd = CreateWindowExW(
        0, MAKEINTRESOURCEW(g_pick_class), L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 22, 24,
        fz->host_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlPickBtnId)),
        hinst, nullptr);
    if (fz->pick_btn_hwnd)
        SetWindowLongPtrW(fz->pick_btn_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fz));

    fz->edit_hwnd = CreateWindowExW(
        0, L"EDIT", L"10 pt",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
        22, 0, 54, 24,
        fz->host_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlEditId)),
        hinst, nullptr);

    fz->updown_hwnd = CreateWindowExW(
        0, UPDOWN_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
        0, 0, 18, 24,
        fz->host_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlUpDownId)),
        hinst, nullptr);

    if (fz->edit_hwnd && fz->ui_font)
        SendMessageW(fz->edit_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(fz->ui_font), FALSE);

    if (fz->updown_hwnd && fz->edit_hwnd) {
        SendMessageW(fz->updown_hwnd, UDM_SETBUDDY, reinterpret_cast<WPARAM>(fz->edit_hwnd), 0);
        SendMessageW(fz->updown_hwnd, UDM_SETRANGE32, kMinFontPt, kMaxFontPt);
    }

    if (fz->edit_hwnd) {
        SetWindowLongPtrW(fz->edit_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fz));
        fz->orig_edit_proc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(fz->edit_hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(edit_subclass_proc)));
    }
    if (fz->updown_hwnd) {
        SetWindowLongPtrW(fz->updown_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fz));
        fz->orig_updown_proc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(fz->updown_hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(updown_subclass_proc)));
    }

    fz->preview_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        MAKEINTRESOURCEW(g_preview_class), L"",
        WS_POPUP,
        0, 0, 10, 10,
        nullptr, nullptr, hinst, nullptr);

    fontzoom_sync_text(fz);
    fontzoom_update_layout(fz);
    return fz;
}

void fontzoom_destroy(FontZoomState *fz) {
    if (!fz)
        return;
    cancel_pick_drag(fz);
    cancel_preview(fz);
    if (fz->edit_hwnd && fz->orig_edit_proc) {
        SetWindowLongPtrW(fz->edit_hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(fz->orig_edit_proc));
    }
    if (fz->updown_hwnd && fz->orig_updown_proc) {
        SetWindowLongPtrW(fz->updown_hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(fz->orig_updown_proc));
    }
    if (fz->preview_hwnd)
        DestroyWindow(fz->preview_hwnd);
    if (fz->host_hwnd)
        DestroyWindow(fz->host_hwnd);
    if (fz->ui_font)
        DeleteObject(fz->ui_font);
    delete fz;
}

void fontzoom_sync_text(FontZoomState *fz) {
    if (!fz || fz->preview_active)
        return;
    int pt = fz->cb.get_current_pt ? fz->cb.get_current_pt(fz->ctx) : 10;
    set_edit_pt_text(fz, pt);
}

void fontzoom_update_layout(FontZoomState *fz) {
    if (!fz || !fz->host_hwnd || !fz->term_hwnd)
        return;

    LONG_PTR style = GetWindowLongPtrW(fz->term_hwnd, GWL_STYLE);
    if ((style & WS_CHILD) || !(style & WS_CAPTION) ||
        !IsWindowVisible(fz->term_hwnd) || IsIconic(fz->term_hwnd)) {
        cancel_preview(fz);
        ShowWindow(fz->host_hwnd, SW_HIDE);
        return;
    }

    bool zoomed = IsZoomed(fz->term_hwnd) != FALSE;
    if (zoomed)
        cancel_preview(fz);

    if (fz->pick_btn_hwnd)
        EnableWindow(fz->pick_btn_hwnd, zoomed ? FALSE : TRUE);
    if (fz->edit_hwnd)
        EnableWindow(fz->edit_hwnd, zoomed ? FALSE : TRUE);
    if (fz->updown_hwnd)
        EnableWindow(fz->updown_hwnd, zoomed ? FALSE : TRUE);

    UINT dpi = query_window_dpi(fz->term_hwnd);
    if (dpi != fz->current_dpi) {
        fz->current_dpi = dpi;
        HFONT new_font = create_ui_font(dpi);
        if (new_font) {
            if (fz->edit_hwnd)
                SendMessageW(fz->edit_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(new_font), TRUE);
            if (fz->ui_font)
                DeleteObject(fz->ui_font);
            fz->ui_font = new_font;
        }
    }

    RECT wr = {};
    GetWindowRect(fz->term_hwnd, &wr);

    RECT cap_rc = {};
    HRESULT hr = DwmGetWindowAttribute(
        fz->term_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &cap_rc, sizeof(cap_rc));

    int cap_left = 0;
    int cap_top = 0;
    int cap_h = 0;
    if (SUCCEEDED(hr) && cap_rc.right > cap_rc.left && cap_rc.bottom > cap_rc.top) {
        cap_left = cap_rc.left;
        cap_top = cap_rc.top;
        cap_h = cap_rc.bottom - cap_rc.top;
    } else {
        int btn_w = MulDiv(46, static_cast<int>(dpi), 96);
        cap_h = MulDiv(30, static_cast<int>(dpi), 96);
        cap_left = (wr.right - wr.left) - btn_w * 3 - MulDiv(8, static_cast<int>(dpi), 96);
        cap_top = 0;
    }

    int vpad = MulDiv(6, static_cast<int>(dpi), 96);
    int host_h = cap_h - vpad;
    if (host_h < MulDiv(18, static_cast<int>(dpi), 96))
        host_h = MulDiv(18, static_cast<int>(dpi), 96);
    if (host_h > cap_h)
        host_h = cap_h;

    int btn_w = host_h;
    int zoom_w = MulDiv(72, static_cast<int>(dpi), 96);
    int host_w = btn_w + zoom_w;

    int gap = MulDiv(6, static_cast<int>(dpi), 96);
    int host_x = wr.left + cap_left - host_w - gap;
    int min_x = wr.left + MulDiv(48, static_cast<int>(dpi), 96);
    if (host_x < min_x)
        host_x = min_x;
    int host_y = wr.top + cap_top + (cap_h - host_h) / 2;

    SetWindowPos(fz->host_hwnd, nullptr, host_x, host_y, host_w, host_h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if (fz->pick_btn_hwnd)
        MoveWindow(fz->pick_btn_hwnd, 0, 0, btn_w, host_h, TRUE);

    if (fz->edit_hwnd && fz->updown_hwnd) {
        int ud_w = MulDiv(18, static_cast<int>(dpi), 96);
        if (ud_w >= zoom_w - 16)
            ud_w = zoom_w / 3;
        MoveWindow(fz->edit_hwnd, btn_w, 0, zoom_w - ud_w, host_h, TRUE);
        MoveWindow(fz->updown_hwnd, btn_w + zoom_w - ud_w, 0, ud_w, host_h, TRUE);
    }
}

bool fontzoom_try_handle_wheel(FontZoomState *fz, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!fz || !fz->host_hwnd || !IsWindowVisible(fz->host_hwnd))
        return false;
    if (message != WM_MOUSEWHEEL)
        return false;
    bool ctrl = (LOWORD(wParam) & MK_CONTROL) != 0 ||
                (GetKeyState(VK_CONTROL) < 0);
    if (!ctrl)
        return false;

    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT hr = {};
    GetWindowRect(fz->host_hwnd, &hr);
    if (!PtInRect(&hr, pt))
        return false;

    on_ctrl_wheel(fz, static_cast<short>(HIWORD(wParam)));
    return true;
}
