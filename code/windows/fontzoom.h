#ifndef PUTTY_WINDOWS_FONTZOOM_H
#define PUTTY_WINDOWS_FONTZOOM_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontZoomState FontZoomState;

typedef struct FontZoomCallbacks {
    void (*apply_pt)(void *ctx, int target_pt);
    int (*get_base_pt)(void *ctx);
    int (*get_current_pt)(void *ctx);
    bool (*measure_cell_for_pt)(void *ctx, int target_pt, int *out_cell_w, int *out_cell_h);
    void (*get_term_grid)(void *ctx, int *out_cols, int *out_rows, int *out_extra_w, int *out_extra_h);
    void (*show_tip)(HWND anchor, const char *text, int duration_ms);
    void (*hide_tip)(void);
    bool (*can_pick_window)(void *ctx, HWND target_hwnd);
    void (*pick_font_from_window)(void *ctx, HWND target_hwnd);
} FontZoomCallbacks;

FontZoomState *fontzoom_create(HWND term_hwnd, const FontZoomCallbacks *cb, void *ctx);
void fontzoom_destroy(FontZoomState *fz);
void fontzoom_update_layout(FontZoomState *fz);
void fontzoom_sync_text(FontZoomState *fz);
bool fontzoom_try_handle_wheel(FontZoomState *fz, UINT message, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif /* PUTTY_WINDOWS_FONTZOOM_H */
