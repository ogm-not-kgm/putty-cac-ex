#ifndef PUTTY_WINDOWS_TSF_H
#define PUTTY_WINDOWS_TSF_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TsfState TsfState;

typedef enum TsfPreeditStyle {
    TSF_PREEDIT_NONE = 0,
    TSF_PREEDIT_DOT = 1,
    TSF_PREEDIT_SOLID = 2,
    TSF_PREEDIT_TARGET = 3,
} TsfPreeditStyle;

typedef struct TsfHostCallbacks {
    void (*on_ime_mode_changed)(void *ctx, bool open);
    void (*on_commit_text)(void *ctx, const wchar_t *text, int len);
    void (*on_preedit_changed)(void *ctx, const wchar_t *text,
                               const uint8_t *styles, int len, int caret_pos);
    void (*get_context_text)(void *ctx, wchar_t *before_buf, int before_max,
                             int *before_len, wchar_t *after_buf, int after_max,
                             int *after_len);
    int (*measure_text_cells)(void *ctx, const wchar_t *text, int len);
    bool (*get_text_ext)(void *ctx, int preedit_cols, int start_col_offset,
                         int end_col_offset, RECT *out_screen_rect);
} TsfHostCallbacks;

/*
 * Initialize TSF state for a terminal window.
 * Returns NULL if TSF cannot be initialized (caller falls back to IMM32).
 */
TsfState *tsf_init(HWND hwnd, void *host_ctx, const TsfHostCallbacks *callbacks);
void tsf_deinit(TsfState *tsf);

/* True when TSF ThreadMgr and sinks are active. */
bool tsf_is_active(const TsfState *tsf);

/* True when TSF owns the document/text store and handles composition. */
bool tsf_is_composition_active(const TsfState *tsf);

bool tsf_is_ime_open(const TsfState *tsf);

void tsf_on_set_focus(TsfState *tsf);
void tsf_on_kill_focus(TsfState *tsf);
void tsf_on_inputlang_change(TsfState *tsf);
void tsf_notify_cursor_moved(TsfState *tsf);
bool tsf_filter_key(TsfState *tsf, UINT msg, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif
