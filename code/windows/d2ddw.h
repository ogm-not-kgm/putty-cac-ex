//
//

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Background */
void bg_init(WinGuiSeat *wgs);
void bg_create(WinGuiSeat *wgs);
void bg_release();
void bg_resize(WinGuiSeat *wgs, int draw);
void bg_move(WinGuiSeat *wgs, int draw);
void bg_order(WinGuiSeat *wgs);
void bg_glass(WinGuiSeat *wgs);
void bg_trans();
void icon_create(HINSTANCE hinst, int width, int height);
void icon_release();

extern INT bg_active/* = BG_ACTIVE*/;
extern int bm_width;
extern int bm_height;

extern FLOAT pixelsPerDip;// = 1.0f;
extern int font_size;
extern int font_ascent;// = 0;
extern int font_descent;// = 0;
extern int font_underline;// = 0;

#define D2D_RGB(r, g, b)                                                       \
  ((UINT32)(((BYTE)(b) | ((WORD)((BYTE)(g)) << 8)) |                           \
            (((DWORD)(BYTE)(r)) << 16)))

bool window_begin(WinGuiSeat *wgs);
void window_end(WinGuiSeat *wgs);

void d2d_create();
void d2d_release();
void d2d_init(WinGuiSeat *wgs);
void d2d_deinit();
bool d2d_begin(WinGuiSeat *wgs);
void d2d_end(WinGuiSeat *wgs);
void d2d_present(WinGuiSeat *wgs);
void d2d_invalidate(void);
bool d2d_is_invalidated(void);
void d2d_clear(WinGuiSeat *wgs);
void d2d_resize(WinGuiSeat *wgs);

void dw_create();
void dw_release();
void dw_init(WinGuiSeat *wgs, int size);
void dw_deinit();

typedef struct {
    int font_size;          /* DirectWrite Em size (px) */
    int font_height;        /* Cell height (ascent + descent + line_gap) */
    int font_width;         /* Cell width */
    int descent;            /* Baseline / underline Y offset (clamped) */
    int strikethrough_y;    /* Strikethrough Y offset */
    bool font_varpitch;
    bool font_dualwidth;
} DwFontMetrics;

void dw_init_with_metrics(WinGuiSeat *wgs, int pick_width, int pick_height, DwFontMetrics *out);
int dw_get_base_font_pt(WinGuiSeat *wgs);
int dw_get_font_pt(WinGuiSeat *wgs);
bool dw_measure_cell_for_pt(WinGuiSeat *wgs, int target_pt, int *out_cell_w, int *out_cell_h);
bool dw_get_imm_logfont(WinGuiSeat *wgs, LOGFONTW *lfw);
void dw_textout(WinGuiSeat *wgs, int x, int y, UINT opt, const RECT *rc, WCHAR *str,
                       UINT len, const int *dx, int wide,
                       unsigned long long attr);

// cache
extern int baseline_offset;
extern int baseline_offset_fw;
extern int border_style;
extern int cleartype_level;
extern int enhanced_contrast;
extern int font_quality;
extern int gamma;
extern int line_gap;
extern int rendering_mode;
extern bool use_altfont;
extern bool use_widefont;
extern int window_border;

extern FLOAT alphas[4][2];
extern int bg_effect;
extern int bg_wallpaper;
extern int wp_position;
extern int wp_align;
extern int wp_valign;
extern int wp_moving;

void d2d_setcolor_fg(COLORREF c, FLOAT a);
void d2d_setcolor_bg(COLORREF c, FLOAT a);
void d2d_setcolor_c(COLORREF c, FLOAT a);
void d2d_drawrectangle(int x, int y, int w, int h);
void d2d_drawline(int x1, int y1, int x2, int y2);
void d2d_draw_hline_fg(int x1, int x2, int y, COLORREF colour, FLOAT alpha);
void d2d_drawicon(int x, int y, int w, int h);
int dw_char_width(int uc, int font_width);



#ifdef D2DBG_ENABLE
static VOID WINAPI WriteDebugInfo(const char *format, ...) {
  static char path[MAX_PATH];
  FILE *fp;

  GetCurrentDirectory(MAX_PATH, path);
  strcat(path, "\\debug.log");

  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
  fflush(NULL);

  fp = fopen(path, "a+");
  if (fp) {
    va_list ap2;
    va_start(ap2, format);
    vfprintf(fp, format, ap2);
    va_end(ap2);
    fclose(fp);
  }
}
#undef D2DBG
#define D2DBG WriteDebugInfo
#else
#ifndef D2DBG
#define D2DBG(...) ((void)0)
#endif
#endif


#ifdef __cplusplus
}
#endif
