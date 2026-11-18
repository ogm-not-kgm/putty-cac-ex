//
//

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <assert.h>
#include <wchar.h>

extern "C" {
#include "putty.h"
#include "putty-rc.h"
#include "win-gui-seat.h"
}

#include <dwmapi.h>
#include <uxtheme.h>
#include <winuser.h>

/* Background */
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_2.h>
#include <d2d1_3.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wincodecsdk.h>

#include "d2ddw.h"

/*
void bg_init(WinGuiSeat *wgs);
void bg_create(WinGuiSeat *wgs);
void bg_release();
void bg_resize(WinGuiSeat *wgs, int draw);
void bg_move(WinGuiSeat *wgs, int draw);
void bg_order(WinGuiSeat *wgs);
void bg_glass(WinGuiSeat *wgs);
void bg_trans();
*/
void bm_create(WinGuiSeat *wgs);
void bm_release();
void wp_create(WinGuiSeat *wgs);
void wp_release();
void wp_draw_impl(WinGuiSeat *wgs, ID2D1RenderTarget *rt, const D2D1_RECT_F *rect, bool rounded);
/*
void icon_create();
void icon_release();

INT bg_active = BG_ACTIVE;
int bm_width;
int bm_height;
*/


// d2d
ID2D1Bitmap *d2dBM = NULL;
ID2D1BitmapBrush *d2dBB = NULL;

ID2D1Factory *d2dF = NULL;
IDWriteFactory *dwF = NULL;
IDWriteFactory4 *dwF4 = NULL;
ID2D1StrokeStyle *d2dSS = NULL;

ID2D1HwndRenderTarget *d2dRT = NULL;
ID2D1DeviceContext *d2dRTDC = NULL;
ID2D1BitmapRenderTarget *d2dBRT = NULL;
ID2D1DeviceContext4 *d2dDC4 = NULL;
IDWriteRenderingParams *d2d_text_params = NULL;
bool d2d_need_full_clear = true;
bool d2d_cleared_in_frame = false;
ID2D1SolidColorBrush *d2dSCBfg = NULL;
ID2D1SolidColorBrush *d2dSCBbg = NULL;
ID2D1SolidColorBrush *d2dSCBc = NULL;
ID2D1SolidColorBrush *d2dSCBcol = NULL;

ID2D1Bitmap *d2dICON = NULL;

enum { FF_NORMAL, FF_WIDE, FF_ALT };
enum { FF_BOLD = 1 };

IDWriteFontFace *dwFF[3][2] = {NULL, NULL, NULL, NULL, NULL, NULL};

FLOAT dw_width[3][2][0x10000];
BOOL dw_color[3];

BOOL alt_segoeuiemoji = FALSE;
FLOAT alt_offset = 0;

void dw_fontface(WinGuiSeat *wgs, const WCHAR *name, int charset, int size, int weight,
                        IDWriteFontFace **fontface, BOOL *color);
FLOAT dw_getwidth(UINT32 index, IDWriteFontFace *fontface, BOOL segoeuiemoji);

LCID getLCID(DWORD CharSet);

struct AccentPolicy {
  DWORD AccentState;
  DWORD AccentFlags;
  DWORD GradientColor;
  DWORD AnimationId;
};

struct WINCOMPATTRDATA {
  DWORD attribute;
  PVOID pData;
  ULONG dataSize;
};

DECL_WINDOWS_FUNCTION(static, HRESULT, SetWindowCompositionAttribute,
                      (HWND, WINCOMPATTRDATA *));

/*
// cache
int baseline_offset;
int baseline_offset_fw;
int border_style;
int cleartype_level;
int enhanced_contrast;
int font_quality;
int gamma;
int line_gap;
int rendering_mode;
bool use_altfont;
bool use_widefont;
int window_border;
*/







void d2d_create() {
  D2DBG("d2d_create:\n");

  d2d_release();

  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                    0, (void **)&d2dF);
  float dashes[] = {1.0f, 2.0f};
  D2D1_STROKE_STYLE_PROPERTIES properties = D2D1::StrokeStyleProperties(
      D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_SQUARE,
      D2D1_LINE_JOIN_MITER, 1.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);
  d2dF->CreateStrokeStyle(properties, dashes, ARRAYSIZE(dashes), &d2dSS);
}

void d2d_release() {
  d2d_deinit();

  if (d2dF) {
    d2dF->Release();
    d2dF = NULL;
  }
  if (d2dSS) {
    d2dSS->Release();
    d2dSS = NULL;
  }
}

static void d2d_init_brt(WinGuiSeat *wgs) {
  if (d2dDC4) {
    d2dDC4->Release();
    d2dDC4 = NULL;
  }
  if (d2dBRT) {
    d2dBRT->Release();
    d2dBRT = NULL;
  }
  if (!d2dRT || !wgs || !wgs->term_hwnd) {
    return;
  }

  RECT rc;
  GetClientRect(wgs->term_hwnd, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) {
    return;
  }

  HRESULT hr = d2dRT->CreateCompatibleRenderTarget(
      D2D1::SizeF((FLOAT)w, (FLOAT)h), &d2dBRT);
  if (FAILED(hr) || !d2dBRT) {
    d2dBRT = NULL;
    return;
  }

  if (d2d_text_params) {
    d2dBRT->SetTextRenderingParams(d2d_text_params);
  }
  d2dBRT->SetDpi(96.0f, 96.0f);
  d2dBRT->SetTextAntialiasMode(D2D_FONT_QUALITY(font_quality));
  d2dBRT->QueryInterface(reinterpret_cast<ID2D1DeviceContext4 **>(&d2dDC4));
  d2d_need_full_clear = true;
}

void d2d_invalidate(void) {
  d2d_need_full_clear = true;
}

bool d2d_is_invalidated(void) {
  return d2d_need_full_clear;
}

void d2d_init(WinGuiSeat *wgs) {
  D2DBG("d2d_init:\n");
  HWND hwnd = wgs->term_hwnd;

  d2d_deinit();

  RECT rc;
  GetClientRect(hwnd, &rc);
  D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
  const D2D1_PIXEL_FORMAT format =
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        // D2D1_ALPHA_MODE_UNKNOWN);
                        // D2D1_ALPHA_MODE_STRAIGHT);
                        // D2D1_ALPHA_MODE_IGNORE);
                        D2D1_ALPHA_MODE_PREMULTIPLIED);
  CONST D2D1_RENDER_TARGET_PROPERTIES targetProperties =
      D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, format);
  CONST D2D1_HWND_RENDER_TARGET_PROPERTIES windowProperties =
      D2D1::HwndRenderTargetProperties(hwnd, size,
                                       // D2D1_PRESENT_OPTIONS_NONE);
                                       D2D1_PRESENT_OPTIONS_IMMEDIATELY);
  d2dF->CreateHwndRenderTarget(targetProperties, windowProperties, &d2dRT);

  DWRITE_RENDERING_MODE rendering_mode_dw = DW_RENDERING_MODE(rendering_mode);
  if (D2D_FONT_QUALITY(font_quality) == D2D1_TEXT_ANTIALIAS_MODE_ALIASED) {
    rendering_mode_dw = DWRITE_RENDERING_MODE_ALIASED;
  }
  if ((D2D_FONT_QUALITY(font_quality) == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE) &&
      (rendering_mode_dw == DWRITE_RENDERING_MODE_OUTLINE)) {
    rendering_mode_dw = DWRITE_RENDERING_MODE_DEFAULT;
  }

  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  IDWriteRenderingParams *dwrpm = NULL;
  dwF->CreateMonitorRenderingParams(mon, &dwrpm);
  dwF->CreateCustomRenderingParams( // dwrpm->GetGamma(),
                                    // dwrpm->GetEnhancedContrast(),
                                    // dwrpm->GetClearTypeLevel(),
      (FLOAT)gamma / 1000.0f, (FLOAT)enhanced_contrast / 100.0f,
      (FLOAT)cleartype_level / 100.0f, dwrpm->GetPixelGeometry(),
      rendering_mode_dw, &d2d_text_params);
  d2dRT->SetTextRenderingParams(d2d_text_params);
  d2dRT->SetDpi(96.0f, 96.0f);
  dwrpm->Release();

  static const int CONF_alphas_pc[4][2] = {
      {CONF_alphas_pc_cursor_active, CONF_alphas_pc_cursor_inactive},
      {CONF_alphas_pc_defauly_fg_active, CONF_alphas_pc_defauly_fg_inactive},
      {CONF_alphas_pc_degault_bg_active, CONF_alphas_pc_degault_bg_inactive},
      {CONF_alphas_pc_bg_active, CONF_alphas_pc_bg_inactive}};
  int i, j;
  for (i = 0; i < 4; i++) {
    for (j = 0; j < 2; j++) {
      if (conf_get_int(wgs->conf, CONF_alphas_pc[i][j]) > 100) {
        conf_set_int(wgs->conf, CONF_alphas_pc[i][j], 100);
      }
      if (conf_get_int(wgs->conf, CONF_alphas_pc[i][j]) < 0) {
        conf_set_int(wgs->conf, CONF_alphas_pc[i][j], 0);
      }
      alphas[i][j] = (FLOAT)conf_get_int(wgs->conf, CONF_alphas_pc[i][j]) / 100;
      if ((alphas[i][j] == 0) && (bg_effect == BG_PLANE)) {
        alphas[i][j] = (FLOAT)0.01;
      }
    }
  }

  d2dRT->SetTextAntialiasMode(D2D_FONT_QUALITY(font_quality));

  d2dRT->CreateSolidColorBrush(
      D2D1::ColorF(0.0f, 0.0f, 0.0f, alphas[ALPHA_DEFAULT_FG][BG_ACTIVE]),
      &d2dSCBfg);
  CONST D2D1_BRUSH_PROPERTIES brushProperties =
      D2D1::BrushProperties(1.0f, D2D1::Matrix3x2F::Identity());
  d2dRT->CreateSolidColorBrush(
      D2D1::ColorF(0.0f, 0.0f, 0.0f, alphas[ALPHA_BG][BG_ACTIVE]),
      brushProperties, &d2dSCBbg);
  d2dRT->CreateSolidColorBrush(
      D2D1::ColorF(0.0f, 0.0f, 0.0f, alphas[ALPHA_CURSOR][BG_ACTIVE]),
      &d2dSCBc);
  d2dRT->CreateSolidColorBrush(
      D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
      &d2dSCBcol);
  d2dRT->QueryInterface(reinterpret_cast<ID2D1DeviceContext **>(&d2dRTDC));

  d2d_init_brt(wgs);

  if (wgs->font_width > 0 && wgs->font_height > 0) {
    icon_create(hinst, wgs->font_width * 2, wgs->font_height);
  }
}

void d2d_deinit() {
  icon_release();
  if (d2dDC4) {
    d2dDC4->Release();
    d2dDC4 = NULL;
  }
  if (d2dBRT) {
    d2dBRT->Release();
    d2dBRT = NULL;
  }
  if (d2d_text_params) {
    d2d_text_params->Release();
    d2d_text_params = NULL;
  }
  if (d2dRTDC) {
    d2dRTDC->Release();
    d2dRTDC = NULL;
  }
  if (d2dRT) {
    d2dRT->Release();
    d2dRT = NULL;
  }
  if (d2dSCBfg) {
    d2dSCBfg->Release();
    d2dSCBfg = NULL;
  }
  if (d2dSCBbg) {
    d2dSCBbg->Release();
    d2dSCBbg = NULL;
  }
  if (d2dSCBc) {
    d2dSCBc->Release();
    d2dSCBc = NULL;
  }
  if (d2dSCBcol) {
    d2dSCBcol->Release();
    d2dSCBcol = NULL;
  }
  d2d_need_full_clear = true;
  d2d_cleared_in_frame = false;
}

bool d2d_begin(WinGuiSeat *wgs) {
  if (d2dF != NULL && d2dRT == NULL) {
    // re-create the render target and related resources.
    d2d_init(wgs);
  } else if (d2dRT != NULL && d2dBRT == NULL) {
    d2d_init_brt(wgs);
  }

  if (!d2dRT || !d2dBRT) {
    // TODO: define err code.
    return false;
  }
  d2d_cleared_in_frame = false;
  d2dBRT->BeginDraw();
  return true;
}

void d2d_present(WinGuiSeat *wgs) {
  (void)wgs;
  if (!d2dRT || !d2dBRT) {
    return;
  }
  if (d2dRT->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) {
    return;
  }

  ID2D1Bitmap *brtBitmap = NULL;
  if (FAILED(d2dBRT->GetBitmap(&brtBitmap)) || !brtBitmap) {
    return;
  }

  d2dRT->BeginDraw();
  D2D1_SIZE_F sz = brtBitmap->GetSize();
  D2D1_RECT_F dst = D2D1::RectF(0.0f, 0.0f, sz.width, sz.height);
  if (d2dRTDC) {
    d2dRTDC->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    d2dRTDC->DrawBitmap(brtBitmap, &dst, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    d2dRTDC->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
  } else {
    d2dRT->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    d2dRT->DrawBitmap(brtBitmap, &dst, 1.0f,
                      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  }
  HRESULT hr = d2dRT->EndDraw();
  brtBitmap->Release();

  if (hr == D2DERR_RECREATE_TARGET) {
    // release the render target and related resources(keep d2dF).
    d2d_deinit();
  }
}

void d2d_end(WinGuiSeat *wgs) {
  d2d_cleared_in_frame = false;
  if (!d2dBRT) {
    return;
  }
  HRESULT hr = d2dBRT->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    // release the render target and related resources(keep d2dF).
    d2d_deinit();
    return;
  }
  if (SUCCEEDED(hr)) {
    d2d_present(wgs);
  }
}

void d2d_clear(WinGuiSeat *wgs) {
  // D2DBG("d2d_clear:\n");
  HWND hwnd = wgs->term_hwnd;

  if ((!d2dRT) || (!d2dBRT) ||
      (d2dRT->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED)) {
    D2DBG("d2d_clear: skip\n");
    return;
  }

  D2D1_RECT_F rect;
  RECT cr;
  GetClientRect(hwnd, &cr);

  d2dBRT->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  d2dSCBbg->SetColor(
      D2D1::ColorF(wgs->colours[258], alphas[ALPHA_DEFAULT_BG][BG_ACTIVE]));

  rect = D2D1::RectF((FLOAT)cr.left, (FLOAT)cr.top, (FLOAT)cr.right,
                     (FLOAT)cr.bottom);

  wp_draw_impl(wgs, d2dBRT, &rect, true);
  d2d_need_full_clear = false;
  d2d_cleared_in_frame = true;
}

void d2d_resize(WinGuiSeat *wgs) {
  D2DBG("d2d_resize:\n");
  HWND hwnd = wgs->term_hwnd;

  if (!d2dRT) {
    return;
  }

  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) {
    return;
  }

  D2D1_SIZE_U size = D2D1::SizeU((UINT32)w, (UINT32)h);
  d2dRT->Resize(size);

  // came from bg_resize()
  bg_glass(wgs);
  if (bg_wallpaper == BG_FILE ||
      (bg_wallpaper == BG_FILE_DESKTOP && !d2dBB)) {
    wp_create(wgs);
  }
  d2d_init_brt(wgs);
}

static LCID getLCID(DWORD CharSet) {
  switch (CharSet) {
  case ANSI_CHARSET:
    return 0x0409;
  case DEFAULT_CHARSET:
    return LOCALE_USER_DEFAULT;
  // case SYMBOL_CHARSET: return 0x0;
  case SHIFTJIS_CHARSET:
    return 0x411;
  case HANGEUL_CHARSET:
    return 0x412;
  case GB2312_CHARSET:
    return 0x804;
  case CHINESEBIG5_CHARSET:
    return 0x404;
  // case OEM_CHARSET: return 0x0;
  case JOHAB_CHARSET:
    return 0x812;
  case HEBREW_CHARSET:
    return 0x40d;
  case ARABIC_CHARSET:
    return 0x01;
  case GREEK_CHARSET:
    return 0x408;
  case TURKISH_CHARSET:
    return 0x41f;
  case VIETNAMESE_CHARSET:
    return 0x42a;
  case THAI_CHARSET:
    return 0x41e;
  case EASTEUROPE_CHARSET:
    return 0x405;
  case RUSSIAN_CHARSET:
    return 0x419;
  // case MAC_CHARSET: return 0xFFF;
  case BALTIC_CHARSET:
    return 0x425;
  }
  return LOCALE_USER_DEFAULT;
}

void dw_create() {
  D2DBG("dw_create:\n");

  dw_release();

  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      (IUnknown **)&dwF);
  dwF->QueryInterface(reinterpret_cast<IDWriteFactory4**>(&dwF4));
}

void dw_release() {
  dw_deinit();

  if (dwF4) {
    dwF4->Release();
    dwF4 = NULL;
  }
  if (dwF) {
    dwF->Release();
    dwF = NULL;
  }
}

void dw_init(WinGuiSeat *wgs, int size) {
  D2DBG("dw_init:\n");

  dw_deinit();
  font_size = size;

  //
  WCHAR wfontname[64];
  WCHAR wlocalename[LOCALE_NAME_MAX_LENGTH];
  FontSpec *font;
  BOOL tmp;

  font = conf_get_fontspec(wgs->conf, CONF_font);
  MultiByteToWideChar(CP_UTF8, 0, font->name, -1, (LPWSTR)&wfontname, 64);
  GetLocaleInfoW(getLCID(font->charset), LOCALE_SNAME, &wlocalename[0],
                 ARRAYSIZE(wlocalename));
  dw_fontface(wgs, wfontname, font->charset, size, FW_NORMAL,
              &dwFF[FF_NORMAL][FF_NORMAL], &dw_color[FF_NORMAL]);
  dw_fontface(wgs, wfontname, font->charset, size, FW_BOLD,
              &dwFF[FF_NORMAL][FF_BOLD], &tmp);

  D2DBG("  wfontname=%ls charset=%d wlocalename=%ls\n", wfontname,
        font->charset, wlocalename);

  FontSpec *widefont = conf_get_fontspec(wgs->conf, CONF_widefont);
  if (use_widefont && widefont && widefont->name[0]) {
    MultiByteToWideChar(CP_UTF8, 0, widefont->name, -1, (LPWSTR)&wfontname, 64);
    GetLocaleInfoW(getLCID(widefont->charset), LOCALE_SNAME, &wlocalename[0],
                   ARRAYSIZE(wlocalename));
    dw_fontface(wgs, wfontname, widefont->charset, size, FW_NORMAL,
                &dwFF[FF_WIDE][FF_NORMAL], &dw_color[FF_WIDE]);
    dw_fontface(wgs, wfontname, widefont->charset, size, FW_BOLD,
                &dwFF[FF_WIDE][FF_BOLD], &tmp);
    D2DBG("  wfontname=%ls charset=%d wlocalename=%ls\n", wfontname,
          widefont->charset, wlocalename);
  }

  if (use_altfont) {
    font = conf_get_fontspec(wgs->conf, CONF_altfont);
    MultiByteToWideChar(CP_UTF8, 0, font->name, -1, (LPWSTR)&wfontname, 64);
    GetLocaleInfoW(getLCID(font->charset), LOCALE_SNAME, &wlocalename[0],
                   ARRAYSIZE(wlocalename));
    dw_fontface(wgs, wfontname, font->charset, size, FW_NORMAL,
                &dwFF[FF_ALT][FF_NORMAL], &dw_color[FF_ALT]);
    dw_fontface(wgs, wfontname, font->charset, size, FW_BOLD,
                &dwFF[FF_ALT][FF_BOLD], &tmp);

    if (strcmp(font->name, "Segoe UI Emoji") == 0) {
      alt_segoeuiemoji = TRUE;
    }
  }

  DWRITE_FONT_METRICS fm = {};
  if (dwFF[FF_NORMAL][FF_NORMAL]) {
    dwFF[FF_NORMAL][FF_NORMAL]->GetMetrics(&fm);
  }
  if (fm.designUnitsPerEm == 0) {
    fm.designUnitsPerEm = 1000;
    fm.ascent = 800;
    fm.descent = 200;
  }
  font_ascent =
      INT((FLOAT(fm.ascent * font_size) / fm.designUnitsPerEm) + 0.5f);
  font_descent =
      INT((FLOAT(fm.descent * font_size) / fm.designUnitsPerEm) + 0.5f);
  font_underline = -INT(
      (FLOAT(-fm.underlinePosition * font_size) / fm.designUnitsPerEm) + 0.5f);
  D2DBG("  init_font: Ascent=%d Descent=%d underlinePosition=%d\n", font_ascent, font_descent, font_underline);
}

void dw_deinit() {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
      if (dwFF[i][j]) {
        dwFF[i][j]->Release();
        dwFF[i][j] = NULL;
      }
    }
  }

  memset(dw_width, 0, sizeof(FLOAT) * 3 * 2 * 0x10000);

  alt_segoeuiemoji = FALSE;
  alt_offset = 0;
}

static bool dw_find_font_family(IDWriteFontCollection *collection,
                                const WCHAR *name,
                                IDWriteFontFamily **outFamily) {
  if (!collection || !outFamily)
    return false;
  *outFamily = NULL;

  UINT32 index = 0;
  BOOL exists = FALSE;
  if (name && name[0]) {
    if (SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists) {
      if (SUCCEEDED(collection->GetFontFamily(index, outFamily)) && *outFamily)
        return true;
    }

    /* Localized family name fallback */
    UINT32 count = collection->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; i++) {
      IDWriteFontFamily *fam = NULL;
      if (FAILED(collection->GetFontFamily(i, &fam)) || !fam)
        continue;
      IDWriteLocalizedStrings *names = NULL;
      if (SUCCEEDED(fam->GetFamilyNames(&names)) && names) {
        UINT32 ncount = names->GetCount();
        for (UINT32 j = 0; j < ncount; j++) {
          UINT32 len = 0;
          if (SUCCEEDED(names->GetStringLength(j, &len)) && len > 0 && len < 255) {
            WCHAR buf[256];
            if (SUCCEEDED(names->GetString(j, buf, ARRAYSIZE(buf)))) {
              if (_wcsicmp(buf, name) == 0) {
                *outFamily = fam;
                names->Release();
                return true;
              }
            }
          }
        }
        names->Release();
      }
      fam->Release();
    }
  }

  /* Safe fallback to avoid exit(1) on missing/bitmap fonts */
  static const WCHAR *const fallbacks[] = {L"Consolas", L"MS Gothic"};
  for (size_t k = 0; k < ARRAYSIZE(fallbacks); k++) {
    if (SUCCEEDED(collection->FindFamilyName(fallbacks[k], &index, &exists)) &&
        exists) {
      if (SUCCEEDED(collection->GetFontFamily(index, outFamily)) && *outFamily)
        return true;
    }
  }

  if (collection->GetFontFamilyCount() > 0) {
    if (SUCCEEDED(collection->GetFontFamily(0, outFamily)) && *outFamily)
      return true;
  }
  return false;
}

static void dw_fontface(WinGuiSeat *wgs, const WCHAR *name, int charset,
                        int size, int weight, IDWriteFontFace **fontface,
                        BOOL *color) {
  D2DBG("dw_fontface: %ls\n", name);
  (void)charset;
  (void)size;

  *fontface = NULL;
  *color = FALSE;

  if (!dwF)
    return;

  IDWriteFontCollection *collection = NULL;
  HRESULT hr = dwF->GetSystemFontCollection(&collection, FALSE);
  if (FAILED(hr) || !collection) {
    char *title = dupprintf("%s Fatal Error", appname);
    MessageBox(wgs->term_hwnd, win_strerror(hr), title,
               MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(title);
    exit(1);
  }

  IDWriteFontFamily *family = NULL;
  if (!dw_find_font_family(collection, name, &family)) {
    collection->Release();
    char *title = dupprintf("%s Fatal Error", appname);
    MessageBox(wgs->term_hwnd, "No DirectWrite system fonts available.", title,
               MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(title);
    exit(1);
  }

  DWRITE_FONT_WEIGHT dwWeight = DWRITE_FONT_WEIGHT_NORMAL;
  if (weight >= FW_HEAVY)
    dwWeight = DWRITE_FONT_WEIGHT_HEAVY;
  else if (weight >= FW_BOLD)
    dwWeight = DWRITE_FONT_WEIGHT_BOLD;

  IDWriteFont *dw_f = NULL;
  hr = family->GetFirstMatchingFont(dwWeight, DWRITE_FONT_STRETCH_NORMAL,
                                    DWRITE_FONT_STYLE_NORMAL, &dw_f);
  if (SUCCEEDED(hr) && dw_f) {
    dw_f->CreateFontFace(fontface);

    IDWriteFont2 *dw_f2 = NULL;
    if (SUCCEEDED(dw_f->QueryInterface(reinterpret_cast<IDWriteFont2 **>(&dw_f2))) &&
        dw_f2) {
      *color = dw_f2->IsColorFont();
      dw_f2->Release();
    }
    D2DBG("  color=%d\n", *color);
    dw_f->Release();
  }

  family->Release();
  collection->Release();
}

static FLOAT dw_getwidth(UINT16 index, IDWriteFontFace *fontface, BOOL segoeuiemoji) {
  DWRITE_FONT_METRICS fm = {};
  fontface->GetMetrics(&fm);
  DWRITE_GLYPH_METRICS gm = {};
  if (FAILED(fontface->GetDesignGlyphMetrics(&index, 1, &gm, FALSE)) ||
      fm.designUnitsPerEm == 0) {
    return (FLOAT)font_size / 2.0f;
  }

  if (segoeuiemoji) {
    if (!alt_offset) {
      alt_offset = -(FLOAT)gm.leftSideBearing * font_size / fm.designUnitsPerEm;
    }
    return (FLOAT)(gm.advanceWidth - gm.leftSideBearing - gm.rightSideBearing) * font_size / fm.designUnitsPerEm;
  }

  return (FLOAT)gm.advanceWidth * font_size / fm.designUnitsPerEm;
}

static FLOAT dw_measure_codepoint(IDWriteFontFace *face, UINT32 cp, bool *has_glyph) {
  if (!face) {
    if (has_glyph) *has_glyph = false;
    return 0.0f;
  }
  UINT16 glyphIndex = 0;
  if (FAILED(face->GetGlyphIndices(&cp, 1, &glyphIndex)) || glyphIndex == 0) {
    if (has_glyph) *has_glyph = false;
    return 0.0f;
  }
  if (has_glyph) *has_glyph = true;
  return dw_getwidth(glyphIndex, face, FALSE);
}

static int dw_clamp_pt(int pt) {
  if (pt < 4) return 4;
  if (pt > 72) return 72;
  return pt;
}

int dw_get_base_font_pt(WinGuiSeat *wgs) {
  FontSpec *font = conf_get_fontspec(wgs->conf, CONF_font);
  int dpi_y = wgs->dpi_info.cur_dpi.y > 0 ? wgs->dpi_info.cur_dpi.y : 96;
  int pt = 10;
  if (font) {
    if (font->height > 0) {
      pt = font->height;
    } else if (font->height < 0) {
      pt = MulDiv(-font->height, 72, dpi_y);
    }
  }
  if (pt < 1) pt = 1;
  return pt;
}

int dw_get_font_pt(WinGuiSeat *wgs) {
  int base_pt = dw_get_base_font_pt(wgs);
  if (wgs->font_zoom_delta == 0)
    return base_pt;
  return dw_clamp_pt(base_pt + wgs->font_zoom_delta);
}

bool dw_measure_cell_for_pt(WinGuiSeat *wgs, int target_pt, int *out_cell_w, int *out_cell_h) {
  IDWriteFontFace *face = dwFF[FF_NORMAL][FF_NORMAL];
  if (!face || !wgs)
    return false;

  int dpi_y = wgs->dpi_info.cur_dpi.y > 0 ? wgs->dpi_info.cur_dpi.y : 96;
  int pt = dw_clamp_pt(target_pt);
  int em = MulDiv(pt, dpi_y, 72);
  if (em < 1) em = 1;

  DWRITE_FONT_METRICS fm = {};
  face->GetMetrics(&fm);
  if (fm.designUnitsPerEm == 0) {
    return false;
  }

  int ascent = (int)((FLOAT)fm.ascent * em / fm.designUnitsPerEm + 0.5f);
  int descent_px = (int)((FLOAT)fm.descent * em / fm.designUnitsPerEm + 0.5f);
  int cell_h = ascent + descent_px + line_gap;
  if (cell_h < 1) cell_h = 1;

  int max_digit_w = 0;
  for (UINT32 ch = '0'; ch <= '9'; ch++) {
    UINT16 glyphIndex = 0;
    if (SUCCEEDED(face->GetGlyphIndices(&ch, 1, &glyphIndex)) && glyphIndex != 0) {
      DWRITE_GLYPH_METRICS gm = {};
      if (SUCCEEDED(face->GetDesignGlyphMetrics(&glyphIndex, 1, &gm, FALSE))) {
        int iw = (int)((FLOAT)gm.advanceWidth * em / fm.designUnitsPerEm + 0.5f);
        if (iw > max_digit_w) max_digit_w = iw;
      }
    }
  }
  if (max_digit_w < 1) {
    max_digit_w = (em + 1) / 2;
    if (max_digit_w < 1) max_digit_w = 1;
  }

  if (out_cell_w) *out_cell_w = max_digit_w;
  if (out_cell_h) *out_cell_h = cell_h;
  return true;
}

void dw_init_with_metrics(WinGuiSeat *wgs, int pick_width, int pick_height, DwFontMetrics *out) {
  FontSpec *font = conf_get_fontspec(wgs->conf, CONF_font);
  int target_size = 0;

  if (pick_height == 0) {
    if (wgs->font_zoom_delta == 0 && font->height < 0) {
      target_size = -font->height;
    } else {
      int pt = dw_get_font_pt(wgs);
      int dpi_y = wgs->dpi_info.cur_dpi.y > 0 ? wgs->dpi_info.cur_dpi.y : 96;
      target_size = MulDiv(pt, dpi_y, 72);
    }
    if (target_size < 1) target_size = 1;
    dw_init(wgs, target_size);
  } else if (pick_height < 0) {
    target_size = -pick_height;
    if (target_size < 1) target_size = 1;
    dw_init(wgs, target_size);
  } else {
    dw_init(wgs, pick_height);
    if (dwFF[FF_NORMAL][FF_NORMAL]) {
      DWRITE_FONT_METRICS fm0;
      dwFF[FF_NORMAL][FF_NORMAL]->GetMetrics(&fm0);
      int denom = (int)fm0.ascent + (int)fm0.descent;
      if (denom > 0) {
        int num = pick_height - line_gap;
        if (num < 1) num = 1;
        target_size = (int)((FLOAT)num * fm0.designUnitsPerEm / denom);
        if (target_size < 1) target_size = 1;
        if (target_size != font_size) {
          dw_init(wgs, target_size);
        }
      }
    }
  }

  DWRITE_FONT_METRICS fm = {};
  if (dwFF[FF_NORMAL][FF_NORMAL]) {
    dwFF[FF_NORMAL][FF_NORMAL]->GetMetrics(&fm);
  }
  if (fm.designUnitsPerEm == 0) {
    fm.designUnitsPerEm = 1000;
    fm.ascent = 800;
    fm.descent = 200;
  }

  int ascent = (int)((FLOAT)fm.ascent * font_size / fm.designUnitsPerEm + 0.5f);
  int descent_px = (int)((FLOAT)fm.descent * font_size / fm.designUnitsPerEm + 0.5f);

  out->font_size = font_size;
  out->font_height = (pick_height > 0) ? pick_height : (ascent + descent_px + line_gap);
  if (out->font_height < 1) out->font_height = 1;

  if (fm.strikethroughPosition > 0) {
    out->strikethrough_y = ascent - (int)((FLOAT)fm.strikethroughPosition * font_size / fm.designUnitsPerEm + 0.5f);
  } else {
    out->strikethrough_y = ascent - (ascent * 3 / 8);
  }

  out->descent = ascent + 1;
  if (out->descent >= out->font_height) {
    out->descent = out->font_height - 1;
  }

  IDWriteFontFace *face = dwFF[FF_NORMAL][FF_NORMAL];
  int max_digit_w = 0;
  for (UINT32 ch = '0'; ch <= '9'; ch++) {
    bool hg = false;
    FLOAT w = dw_measure_codepoint(face, ch, &hg);
    int iw = (int)(w + 0.5f);
    if (iw > max_digit_w) max_digit_w = iw;
  }
  if (max_digit_w < 1) {
    max_digit_w = (font_size + 1) / 2;
    if (max_digit_w < 1) max_digit_w = 1;
  }

  out->font_width = (pick_width != 0) ? pick_width : max_digit_w;
  if (out->font_width < 1) out->font_width = 1;

  bool has_i = false, has_W = false;
  int wi = (int)(dw_measure_codepoint(face, 'i', &has_i) + 0.5f);
  int wW = (int)(dw_measure_codepoint(face, 'W', &has_W) + 0.5f);
  out->font_varpitch = (has_i && has_W && wi != wW);

  bool dual = out->font_varpitch;
  if (!dual) {
    static const UINT32 cjk_samples[] = {0x3042, 0x6F22}; /* 'あ', '漢' */
    for (size_t k = 0; k < ARRAYSIZE(cjk_samples); k++) {
      bool hg = false;
      FLOAT cw = dw_measure_codepoint(face, cjk_samples[k], &hg);
      if (hg && (int)(cw + 0.5f) != out->font_width) {
        dual = true;
        break;
      }
    }
  }
  out->font_dualwidth = dual;
}

bool dw_get_imm_logfont(WinGuiSeat *wgs, LOGFONTW *lfw) {
  (void)wgs;
  if (!lfw) return false;
  memset(lfw, 0, sizeof(*lfw));
  if (!dwF || !dwFF[FF_NORMAL][FF_NORMAL]) return false;

  bool ok = false;
  IDWriteGdiInterop *dw_gi = NULL;
  if (SUCCEEDED(dwF->GetGdiInterop(&dw_gi)) && dw_gi) {
    if (SUCCEEDED(dw_gi->ConvertFontFaceToLOGFONT(dwFF[FF_NORMAL][FF_NORMAL], lfw))) {
      lfw->lfHeight = -font_size;
      ok = true;
    }
    dw_gi->Release();
  }
  return ok;
}

void dw_textout(WinGuiSeat *wgs, int x, int y, UINT opt, const RECT *rc, WCHAR *str,
                       UINT len, const int *dx, int wide,
                       unsigned long long attr) {
  (void)y;
  if (!d2dBRT || !rc || len == 0) {
    return;
  }

  int run_width = 0;
  for (UINT k = 0; k < len; k++) {
    if (dx[k] > 0) {
      run_width += dx[k];
    }
  }

  RECT rc2 = *rc;
  rc2.left = x;
  if (run_width > 0 && x + run_width < rc2.right) {
    rc2.right = x + run_width;
  }
  if (rc2.right <= rc2.left || rc2.bottom <= rc2.top) {
    return;
  }

  int baseline_offset_now = wide ? baseline_offset_fw : baseline_offset;
  int width = *dx;
  for (UINT i = 1; i < len; i++) {
    if (width) {
      break;
    } else {
      width = dx[i];
    }
  }

  D2D1_RECT_F clip_r = D2D1::RectF((FLOAT)rc2.left, (FLOAT)rc2.top,
                                   (FLOAT)rc2.right, (FLOAT)rc2.bottom);
  d2dBRT->PushAxisAlignedClip(&clip_r, D2D1_ANTIALIAS_MODE_ALIASED);

  if (!(attr & TATTR_COMBINING)) {
    if (opt & ETO_OPAQUE) {
      FLOAT bg_a = d2dSCBbg->GetColor().a;
      if (bg_a >= 1.0f || d2d_cleared_in_frame) {
        d2dBRT->FillRectangle(&clip_r, d2dSCBbg);
      } else {
        d2dBRT->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        D2D1_COLOR_F saved_bg = d2dSCBbg->GetColor();
        wp_draw_impl(wgs, d2dBRT, &clip_r, false);
        d2dSCBbg->SetColor(saved_bg);
        d2dBRT->FillRectangle(&clip_r, d2dSCBbg);
      }
    } else if (!d2d_cleared_in_frame) {
      if (bg_wallpaper == BG_NONE &&
          alphas[ALPHA_DEFAULT_BG][bg_active] >= 1.0f) {
        wp_draw_impl(wgs, d2dBRT, &clip_r, false);
      } else {
        d2dBRT->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        wp_draw_impl(wgs, d2dBRT, &clip_r, false);
      }
    }
  }

  if (attr & ATTR_UNDER) {
    D2D1_POINT_2F p0 = D2D1::Point2F(
        (FLOAT)rc2.left, (FLOAT)rc2.top + font_ascent - font_underline -
                             baseline_offset_now + 0.5f);
    D2D1_POINT_2F p1 = D2D1::Point2F(
        (FLOAT)rc2.right, (FLOAT)rc2.top + font_ascent - font_underline -
                              baseline_offset_now + 0.5f);
    d2dBRT->DrawLine(p0, p1, d2dSCBfg, 1.0f);
  }
  unsigned long long preedit_style = attr & ATTR_PREEDIT_MASK;
  if (preedit_style) {
    FLOAT uy = (FLOAT)rc2.top + font_ascent - font_underline -
               baseline_offset_now + 0.5f;
    D2D1_POINT_2F p0 = D2D1::Point2F((FLOAT)rc2.left, uy);
    D2D1_POINT_2F p1 = D2D1::Point2F((FLOAT)rc2.right, uy);
    if (preedit_style == ATTR_PREEDIT_TARGET) {
      d2dBRT->DrawLine(p0, p1, d2dSCBfg, 2.0f);
    } else if (preedit_style == ATTR_PREEDIT_SOLID) {
      d2dBRT->DrawLine(p0, p1, d2dSCBfg, 1.0f);
    } else {
      d2dBRT->DrawLine(p0, p1, d2dSCBfg, 1.0f, d2dSS);
    }
  }

  bool all_spaces = true;
  for (UINT k = 0; k < len; k++) {
    if (str[k] != L' ' && str[k] != L'\x3000') {
      all_spaces = false;
      break;
    }
  }
  if (all_spaces) {
    d2dBRT->PopAxisAlignedClip();
    return;
  }
  //
  static UINT wlen = 512;
  static UINT16 *indices[3] = {new UINT16[wlen * 2], new UINT16[wlen * 2],
                               new UINT16[wlen * 2]};
  static FLOAT *advances = new FLOAT[wlen * 2];
  static DWRITE_GLYPH_OFFSET *offsets = new DWRITE_GLYPH_OFFSET[wlen * 2];
  static UINT16 *clusters = new UINT16[wlen];
  static DWRITE_SHAPING_TEXT_PROPERTIES *textprops =
      new DWRITE_SHAPING_TEXT_PROPERTIES[wlen];
  static DWRITE_SHAPING_GLYPH_PROPERTIES *glyphprops =
      new DWRITE_SHAPING_GLYPH_PROPERTIES[wlen * 2];
  if (wlen < len) {
    delete[] indices[0];
    delete[] indices[1];
    delete[] indices[2];
    delete[] advances;
    delete[] offsets;
    delete[] clusters;
    delete[] textprops;
    delete[] glyphprops;
    UINT new_len = len * 2;
    indices[0] = new UINT16[new_len * 2];
    indices[1] = new UINT16[new_len * 2];
    indices[2] = new UINT16[new_len * 2];
    advances = new FLOAT[new_len * 2];
    offsets = new DWRITE_GLYPH_OFFSET[new_len * 2];
    clusters = new UINT16[new_len];
    textprops = new DWRITE_SHAPING_TEXT_PROPERTIES[new_len];
    glyphprops = new DWRITE_SHAPING_GLYPH_PROPERTIES[new_len * 2];
    wlen = new_len;
  }
  //
  wide =
      (wide && use_widefont && dwFF[FF_WIDE][FF_NORMAL]) ? FF_WIDE : FF_NORMAL;
  int weight = (attr & ATTR_BOLD) ? FF_BOLD : FF_NORMAL;
  int alt = (dwFF[FF_ALT][FF_NORMAL]) ? FF_ALT : wide;

  static IDWriteTextAnalyzer *analyzer = NULL;
  if (!analyzer) {
    dwF->CreateTextAnalyzer(&analyzer);
  }
  static const DWRITE_SCRIPT_ANALYSIS script = {0,
                                                DWRITE_SCRIPT_SHAPES_DEFAULT};
  UINT32 remain;

  analyzer->GetGlyphs(str, len, dwFF[wide][weight], FALSE, FALSE, &script, NULL,
                      NULL, NULL, NULL, 0, len * 2, clusters, textprops,
                      indices[wide], glyphprops, &remain);

  for (UINT i = 0; i < remain; i++) {
    if (!indices[wide][i]) {
      analyzer->GetGlyphs(str, len, dwFF[alt][weight], FALSE, FALSE, &script,
                          NULL, NULL, NULL, NULL, 0, len * 2, clusters,
                          textprops, indices[alt], glyphprops, &remain);
      break;
    }
  }

  //
  UINT16 *index[3] = {indices[0], indices[1], indices[2]};
  UINT i;
  FLOAT width_i;
  FLOAT width_0;
  int face_i;
  int face_0;
  UINT16 index_0;
  UINT16 index_i;
  bool large_0;
  bool large_i;

  while (remain) {
    i = 0;
    face_0 = (index[wide][0]) ? wide : alt;
    index_0 = index[face_0][0];

    BOOL segoeuiemoji = wide && (face_0 == FF_ALT) && alt_segoeuiemoji;

    if (dw_width[face_0][weight][index_0] == 0) {
      dw_width[face_0][weight][index_0] =
          dw_getwidth(index_0, dwFF[face_0][weight], segoeuiemoji);
    }
    width_0 = dw_width[face_0][weight][index_0];
    large_0 = (width_0 > (FLOAT)width + 0.5f);

    advances[0] = (large_0) ? width_0 : (FLOAT)width;
    offsets[0].advanceOffset = (segoeuiemoji) ? alt_offset : (large_0) ? 0.0f : (width - width_0) / 2.0f;
    offsets[0].ascenderOffset = 0;

    //
    for (i = 1; i < remain; i++) {
      face_i = (index[wide][i]) ? wide : alt;
      index_i = index[face_i][i];

      if (face_0 != face_i) {
        break;
      }

      if (dw_width[face_i][weight][index_i] == 0) {
        BOOL segoeuiemoji = wide && (face_i == FF_ALT) && alt_segoeuiemoji;
        dw_width[face_i][weight][index_i] =
            dw_getwidth(index_i, dwFF[face_i][weight], segoeuiemoji);
      }
      width_i = dw_width[face_i][weight][index_i];
      large_i = (width_i > (FLOAT)width + 0.5f);

      if ((!large_0 && large_i) || (large_0 && (width_0 != width_i))) {
        break;
      }

      advances[i] = (large_i) ? width_i : (FLOAT)width;
      offsets[i].advanceOffset = (segoeuiemoji) ? alt_offset : (large_i) ? 0.0f : (width - width_i) / 2.0f;
      offsets[i].ascenderOffset = 0;
    }

    //
    if (large_0) {
      D2D1_MATRIX_3X2_F scale =
          D2D1::Matrix3x2F::Scale(D2D1::Size(width / width_0, 1.0f),
                                  D2D1::Point2F((FLOAT)rc2.left, 0.0f));
      d2dBRT->SetTransform(scale);
    }

    D2D1_POINT_2F origin = D2D1::Point2F(
        (FLOAT)rc2.left, (FLOAT)rc2.top + font_ascent - baseline_offset_now);

    DWRITE_GLYPH_RUN g;
    g.fontFace = dwFF[face_0][weight];
    g.fontEmSize = (FLOAT)font_size;
    g.glyphCount = i;
    g.glyphIndices = index[face_0];
    g.glyphAdvances = advances;
    g.glyphOffsets = offsets;
    g.isSideways = FALSE;
    g.bidiLevel = 0;

    HRESULT hr;
    IDWriteColorGlyphRunEnumerator1 *glyphRunEnumerator = NULL;

    // bw font
    if (!dw_color[face_0] || !dwF4) {
      d2dBRT->DrawGlyphRun(origin, &g, d2dSCBfg, DWRITE_MEASURING_MODE_NATURAL);
      goto fin;
    }

    // color font
    hr = dwF4->TranslateColorGlyphRun(
      origin, &g, NULL,
      (DWRITE_GLYPH_IMAGE_FORMATS)0xff,
      DWRITE_MEASURING_MODE_NATURAL, NULL, 0, &glyphRunEnumerator);

    // bw text
    if (FAILED(hr) || !glyphRunEnumerator) {
      if (glyphRunEnumerator) {
        glyphRunEnumerator->Release();
      }
      d2dBRT->DrawGlyphRun(origin, &g, d2dSCBfg, DWRITE_MEASURING_MODE_NATURAL);
      goto fin;
    }

    for (;;) {
      BOOL haveRun;
      if (FAILED(glyphRunEnumerator->MoveNext(&haveRun)) || !haveRun) {
        break;
      }

      DWRITE_COLOR_GLYPH_RUN1 const* colorRun;
      if (FAILED(glyphRunEnumerator->GetCurrentRun(&colorRun))) {
        break;
      }

      if (colorRun->glyphImageFormat & DWRITE_GLYPH_IMAGE_FORMATS_SVG) {
        if (d2dDC4) {
          D2D1_POINT_2F oo = D2D1::Point2F(colorRun->baselineOriginX, colorRun->baselineOriginY);
          d2dDC4->DrawSvgGlyphRun(oo, &colorRun->glyphRun, d2dSCBfg, NULL, 0,
                                  DWRITE_MEASURING_MODE_NATURAL);
        }
        continue;
      }

      if (colorRun->glyphImageFormat &
          (DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE | DWRITE_GLYPH_IMAGE_FORMATS_CFF)) {
        ID2D1SolidColorBrush *brush = d2dSCBfg;
        if (colorRun->paletteIndex != 0xFFFF && d2dSCBcol) {
          d2dSCBcol->SetColor(colorRun->runColor);
          brush = d2dSCBcol;
        }
        D2D1_POINT_2F oo = D2D1::Point2F(colorRun->baselineOriginX, colorRun->baselineOriginY);
        d2dBRT->DrawGlyphRun(oo, &colorRun->glyphRun, brush, DWRITE_MEASURING_MODE_NATURAL);
        continue;
      }
    }
    glyphRunEnumerator->Release();

    if (large_0) {
      d2dBRT->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    remain -= i;
    index[0] += i;
    index[1] += i;
    index[2] += i;
    rc2.left += width * i;
    continue;

    fin:

    if (large_0) {
      d2dBRT->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    remain -= i;
    index[0] += i;
    index[1] += i;
    index[2] += i;
    rc2.left += width * i;
  }

  d2dBRT->PopAxisAlignedClip();
}

/* Background */
/* Transparency */

void bg_init(WinGuiSeat *wgs) {
}

void bg_create(WinGuiSeat *wgs) {
  bg_release();

  bm_create(wgs);
  wp_create(wgs);

  bg_glass(wgs);
  d2d_invalidate();
}

void bg_release() {
  wp_release();
  bm_release();
}

static void bm_create(WinGuiSeat *wgs) {
  D2DBG("bm_create:\n");

  bm_release();

  if ((bg_wallpaper == BG_NONE) | (bg_wallpaper == BG_DESKTOP)) {
    return;
  }

  char *wp_file = conf_get_filename(wgs->conf, CONF_wp_file)->cpath;
  if (wp_file == NULL || strlen(wp_file) == 0) {
    return;
  }

  IWICImagingFactory *wic;
  IWICBitmapDecoder *dec;
  IWICBitmapFrameDecode *frame;
  IWICFormatConverter *converter;

  WCHAR wp_path[MAX_PATH];
  MultiByteToWideChar(CP_UTF8, 0, wp_file, -1, (LPWSTR)&wp_path, MAX_PATH);
  CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                   IID_IWICImagingFactory, reinterpret_cast<void **>(&wic));
  HRESULT hr = wic->CreateDecoderFromFilename(
      wp_path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec);
  if (!SUCCEEDED(hr)) {
    wic->Release();
    nonfatal("\"%s\" not found.", wp_file);
    return;
  }

  dec->GetFrame(0, &frame);
  wic->CreateFormatConverter(&converter);
  converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, NULL, 0.f,
                        WICBitmapPaletteTypeMedianCut);
  d2dRT->CreateBitmapFromWicBitmap(converter, NULL, &d2dBM);

  converter->Release();
  frame->Release();
  dec->Release();
  wic->Release();

  D2D1_SIZE_U s = d2dBM->GetPixelSize();
  bm_width = s.width;
  bm_height = s.height;

  return;
}

static void bm_release() {
  if (d2dBM) {
    d2dBM->Release();
    d2dBM = NULL;
  }
}

static void wp_create(WinGuiSeat *wgs) {
  D2DBG("wp_create\n");
  HWND hwnd = wgs->term_hwnd;

  wp_release();

  if (d2dBM == NULL) {
    return;
  }

  ID2D1BitmapRenderTarget *brt;
  ID2D1Bitmap *wp;

  int win_width = 0;
  int win_height = 0;
  int stretch_width = 0;
  int stretch_height = 0;
  int offset_x = 0;
  int offset_y = 0;

  int stretch = 0;
  RECT rc;

  enum { STRETCH_NONE, STRETCH_WIDTH, STRETCH_HEIGHT, STRETCH_BOTH };

  if (bg_wallpaper == BG_FILE) {
    GetClientRect(hwnd, &rc);
    win_width = rc.right - rc.left;
    win_height = rc.bottom - rc.top;
  } else if (bg_wallpaper == BG_FILE_DESKTOP) {
    win_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    win_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    rc.left = 0;
    rc.top = 0;
    rc.right = win_width;
    rc.bottom = win_height;
  } else {
    return;
  }

  if (win_width <= 0 || win_height <= 0 || !d2dRT) {
    return;
  }

  d2dRT->CreateCompatibleRenderTarget(
      D2D1::SizeF((FLOAT)win_width, (FLOAT)win_height), &brt);

  brt->BeginDraw();
  brt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  d2dSCBbg->SetColor(D2D1::ColorF(wgs->colours[258], 1.0f));
  brt->FillRectangle(
      D2D1::RectF((FLOAT)0, (FLOAT)0, (FLOAT)win_width, (FLOAT)win_height),
      d2dSCBbg);

  switch (wp_position) {
  case WP_FILL:
    stretch = ((win_width * bm_height) > (win_height * bm_width))
                  ? STRETCH_WIDTH
                  : STRETCH_HEIGHT;
    break;
  case WP_FIT:
    stretch = ((win_width * bm_height) > (win_height * bm_width))
                  ? STRETCH_HEIGHT
                  : STRETCH_WIDTH;
    break;
  case WP_STRETCH:
    stretch = STRETCH_BOTH;
    break;
  case WP_TILE:
  case WP_FIX:
    stretch = STRETCH_NONE;
    break;
  }

  switch (stretch) {
  case STRETCH_NONE:
    offset_x = (wp_align == WP_LEFT)     ? 0
               : (wp_align == WP_CENTER) ? (win_width - bm_width) / 2
               : (wp_align == WP_RIGHT)  ? win_width - bm_width
                                         : 0;
    offset_y = (wp_valign == WP_TOP)      ? 0
               : (wp_valign == WP_MIDDLE) ? (win_height - bm_height) / 2
               : (wp_valign == WP_BOTTOM) ? win_height - bm_height
                                          : 0;
    if (wp_position == WP_TILE) {
      int m = offset_x % bm_width;
      int n = offset_y % bm_height;
      int y = n ? n - bm_height : 0;
      for (; y <= win_height - n; y += bm_height) {
        int x = m ? m - bm_width : 0;
        for (; x <= win_width - m; x += bm_width) {
          brt->DrawBitmap(d2dBM,
                          D2D1::RectF((FLOAT)x, (FLOAT)y, (FLOAT)x + bm_width,
                                      (FLOAT)y + bm_height),
                          1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
      }
    } else {
      brt->DrawBitmap(d2dBM,
                      D2D1::RectF((FLOAT)offset_x, (FLOAT)offset_y,
                                  (FLOAT)offset_x + bm_width,
                                  (FLOAT)offset_y + bm_height),
                      1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    break;
  case STRETCH_WIDTH:
    stretch_width = win_width;
    stretch_height = bm_height * win_width / bm_width;
    ;
    offset_x = 0;
    offset_y = (wp_valign == WP_TOP)      ? 0
               : (wp_valign == WP_MIDDLE) ? (win_height - stretch_height) / 2
               : (wp_valign == WP_BOTTOM) ? win_height - stretch_height
                                          : 0;
    brt->DrawBitmap(d2dBM,
                    D2D1::RectF((FLOAT)offset_x, (FLOAT)offset_y,
                                (FLOAT)offset_x + stretch_width,
                                (FLOAT)offset_y + stretch_height),
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    break;
  case STRETCH_HEIGHT:
    stretch_width = bm_width * win_height / bm_height;
    ;
    stretch_height = win_height;
    offset_x = (wp_align == WP_LEFT)     ? 0
               : (wp_align == WP_CENTER) ? (win_width - stretch_width) / 2
               : (wp_align == WP_RIGHT)  ? win_width - stretch_width
                                         : 0;
    offset_y = 0;
    brt->DrawBitmap(d2dBM,
                    D2D1::RectF((FLOAT)offset_x, (FLOAT)offset_y,
                                (FLOAT)offset_x + stretch_width,
                                (FLOAT)offset_y + stretch_height),
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    break;
  case STRETCH_BOTH:
    brt->DrawBitmap(d2dBM,
                    D2D1::RectF((FLOAT)0, (FLOAT)0, (FLOAT)0 + win_width,
                                (FLOAT)0 + win_height),
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    D2D1::RectF((FLOAT)0, (FLOAT)0, (FLOAT)0 + bm_width,
                                (FLOAT)0 + bm_height));
    break;
  }

  brt->EndDraw();
  brt->GetBitmap(&wp);
  brt->CreateBitmapBrush(wp, &d2dBB);

  wp->Release();
  brt->Release();

  return;
}

static void wp_release() {
  if (d2dBB) {
    d2dBB->Release();
    d2dBB = NULL;
  }
}

void wp_draw_impl(WinGuiSeat *wgs, ID2D1RenderTarget *rt,
                  const D2D1_RECT_F *rect, bool rounded) {
  HWND hwnd = wgs->term_hwnd;

  if (!rt) {
    return;
  }

  if (bg_wallpaper == BG_NONE) {
    d2dSCBbg->SetColor(
        D2D1::ColorF(wgs->colours[258], alphas[ALPHA_DEFAULT_BG][bg_active]));
    if (rounded && (bg_effect == BG_GLASS) && (border_style == BS_ROUNDED)) {
      FLOAT w = (FLOAT)window_border;
      D2D1_ROUNDED_RECT rrect = {*rect, w, w};
      rt->FillRoundedRectangle(rrect, d2dSCBbg);
    } else {
      rt->FillRectangle(rect, d2dSCBbg);
    }
    return;
  }

  if (!d2dBB) {
    return;
  }

  d2dBB->SetOpacity(alphas[ALPHA_DEFAULT_BG][bg_active]);

  if (bg_wallpaper == BG_FILE_DESKTOP) {
    POINT po = {0, 0};
    ClientToScreen(hwnd, &po);
    d2dBB->SetTransform(
        D2D1::Matrix3x2F::Translation(-(FLOAT)po.x, -(FLOAT)po.y));
  }

  if ((bg_wallpaper == BG_FILE) || (bg_wallpaper == BG_FILE_DESKTOP)) {
    if (rounded && (bg_effect == BG_GLASS) && (border_style == BS_ROUNDED)) {
      FLOAT w = (FLOAT)window_border;
      D2D1_ROUNDED_RECT rrect = {*rect, w, w};
      rt->FillRoundedRectangle(rrect, d2dBB);
    } else {
      rt->FillRectangle(rect, d2dBB);
    }
  }

  if (bg_wallpaper == BG_FILE_DESKTOP) {
    d2dBB->SetTransform(D2D1::Matrix3x2F::Identity());
  }

  if (bg_wallpaper == BG_DESKTOP) {
  }
  return;
}

void bg_resize(WinGuiSeat *wgs, int draw) {
}

void bg_move(WinGuiSeat *wgs, int draw) {
  if ((draw || wp_moving) &&
      ((bg_wallpaper == BG_FILE_DESKTOP) || (bg_wallpaper == BG_DESKTOP))) {
    d2d_invalidate();
    term_update(wgs->term);
  }
}

void bg_order(WinGuiSeat *wgs) {
}

void bg_glass(WinGuiSeat *wgs) {
  D2DBG("bg_glass:\n");

  HWND hwnd = wgs->term_hwnd;

  BOOL b = FALSE;
  DwmIsCompositionEnabled(&b);
  if (!b) {
    D2DBG("bg_glass: 0\n");
    return;
  }

  D2DBG("bg_glass: osMajorVersion=%d\n", osMajorVersion);
  D2DBG("bg_glass: osMinorVersion=%d\n", osMinorVersion);
  // Windows 10
  if (osMajorVersion > 9) {
    D2DBG("bg_glass: 1\n");
    if (bg_effect == BG_PLANE) {
      return;
    }
    if (!p_SetWindowCompositionAttribute) {
      D2DBG("bg_glass: 2\n");
      HMODULE hmLib = NULL;
      hmLib = load_system32_dll("user32.dll");
      if (hmLib) {
        D2DBG("bg_glass: 3\n");
        GET_WINDOWS_FUNCTION_NO_TYPECHECK(hmLib, SetWindowCompositionAttribute);
      }
    }
    AccentPolicy policy = {3, 0, 0, 0};
    WINCOMPATTRDATA data = {19, &policy, sizeof(policy)};
    p_SetWindowCompositionAttribute(hwnd, &data);
    D2DBG("bg_glass: done\n");
    return;
  }

  D2DBG("bg_glass: x\n");
  // Windows Vista, 7
  RECT rc;
  GetClientRect(hwnd, &rc);

  HRGN hrgn;
  if (bg_effect == BG_PLANE) {
    hrgn = CreateRectRgn(-1, -1, 1, 1);
  } else {
    hrgn =
        CreateRectRgn(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
  }
  DWM_BLURBEHIND bb = {0};
  bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
  bb.fEnable = TRUE;
  bb.hRgnBlur = hrgn;
  DwmEnableBlurBehindWindow(hwnd, &bb);
  if (hrgn) {
    DeleteObject(hrgn);
  }

  MARGINS margins = {0};
  if ((border_style != BS_NORMAL) && (bg_effect != BG_PLANE)) {
    margins.cxLeftWidth = -1;
  }
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}

void bg_trans() {
}

void icon_create(HINSTANCE hinst, int width, int height) {
  D2DBG("icon_create:\n");

  icon_release();

  if (!d2dRT || width <= 0 || height <= 0)
    return;

  HICON hIcon = (HICON)LoadImage(hinst, MAKEINTRESOURCE(IDI_MAINICON),
                                 IMAGE_ICON, width, height, LR_DEFAULTCOLOR);
  if (!hIcon)
    return;

  IWICImagingFactory *wic = NULL;
  IWICBitmap *wb = NULL;
  IWICFormatConverter *converter = NULL;

  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL,
                                CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void **>(&wic));
  if (SUCCEEDED(hr) && wic) {
    hr = wic->CreateBitmapFromHICON(hIcon, &wb);
    if (SUCCEEDED(hr) && wb) {
      hr = wic->CreateFormatConverter(&converter);
      if (SUCCEEDED(hr) && converter) {
        hr = converter->Initialize(wb, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, NULL, 0.f,
                                   WICBitmapPaletteTypeMedianCut);
        if (SUCCEEDED(hr)) {
          d2dRT->CreateBitmapFromWicBitmap(converter, NULL, &d2dICON);
        }
        converter->Release();
      }
      wb->Release();
    }
    wic->Release();
  }

  DestroyIcon(hIcon);
  return;
}

void icon_release() {
  if (d2dICON) {
    d2dICON->Release();
    d2dICON = NULL;
  }
}


void d2d_setcolor_fg(COLORREF c, FLOAT a) {
  if (d2dSCBfg) {
    // d2dSCBfg->SetColor(D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.8f));
    d2dSCBfg->SetColor(D2D1::ColorF(c, a));
  }
}

void d2d_setcolor_bg(COLORREF c, FLOAT a) {
  if (d2dSCBbg) {
    // d2dSCBbg->SetColor(D2D1::ColorF(0.0f, 0.0f, 1.0f, 0.1f));
    d2dSCBbg->SetColor(D2D1::ColorF(c, a));
  }
}

void d2d_setcolor_c(COLORREF c, FLOAT a) {
  if (d2dSCBc) {
    d2dSCBc->SetColor(D2D1::ColorF(c, a));
  }
}

void d2d_drawrectangle(int x, int y, int w, int h) {
  if (!d2dBRT || !d2dSCBc)
    return;
  D2D1_RECT_F r = D2D1::RectF((FLOAT)x + 0.5f, (FLOAT)y + 0.5f,
                              (FLOAT)w + 0.5f, (FLOAT)h + 0.5f);
  d2dBRT->DrawRectangle(&r, d2dSCBc, 1.0f);
}

void d2d_drawline(int x1, int y1, int x2, int y2) {
  if (!d2dBRT || !d2dSCBc)
    return;
  d2dBRT->DrawLine(
    D2D1::Point2F((FLOAT)x1, (FLOAT)y1),
    D2D1::Point2F((FLOAT)x2, (FLOAT)y2),
    d2dSCBc, 2.0f);
}

void d2d_draw_hline_fg(int x1, int x2, int y, COLORREF colour, FLOAT alpha) {
  if (!d2dBRT || !d2dSCBfg)
    return;
  d2dSCBfg->SetColor(D2D1::ColorF(colour, alpha));
  d2dBRT->DrawLine(
    D2D1::Point2F((FLOAT)x1, (FLOAT)y + 0.5f),
    D2D1::Point2F((FLOAT)x2, (FLOAT)y + 0.5f),
    d2dSCBfg, 1.0f);
}

int dw_char_width(int uc, int font_width) {
  if (!dwFF[FF_NORMAL][FF_NORMAL] || font_width <= 0)
    return 1;

  UINT32 cp = (UINT32)uc;
  UINT16 glyphIndex = 0;
  int face = FF_NORMAL;

  if (FAILED(dwFF[FF_NORMAL][FF_NORMAL]->GetGlyphIndices(&cp, 1, &glyphIndex)) ||
      glyphIndex == 0) {
    glyphIndex = 0;
    if (dwFF[FF_WIDE][FF_NORMAL] &&
        SUCCEEDED(dwFF[FF_WIDE][FF_NORMAL]->GetGlyphIndices(&cp, 1, &glyphIndex)) &&
        glyphIndex != 0) {
      face = FF_WIDE;
    } else if (dwFF[FF_ALT][FF_NORMAL] &&
               SUCCEEDED(dwFF[FF_ALT][FF_NORMAL]->GetGlyphIndices(&cp, 1, &glyphIndex)) &&
               glyphIndex != 0) {
      face = FF_ALT;
    } else {
      return 0;
    }
  }

  FLOAT w = dw_width[face][FF_NORMAL][glyphIndex];
  if (w == 0.0f) {
    w = dw_getwidth(glyphIndex, dwFF[face][FF_NORMAL],
                    (face == FF_ALT) && alt_segoeuiemoji);
    dw_width[face][FF_NORMAL][glyphIndex] = w;
  }

  int cells = ((int)(w + 0.5f) + font_width / 2 - 1) / font_width;
  return (cells < 1) ? 1 : cells;
}

void d2d_drawicon(int x, int y, int w, int h) {
  if (!d2dBRT || !d2dICON)
    return;
  d2dBRT->DrawBitmap(d2dICON,
                     D2D1::RectF((FLOAT)x, (FLOAT)y, (FLOAT)w, (FLOAT)h));
}
