# Direct2D / DirectWrite Rendering Benchmark Results

This document summarizes the rendering performance comparison between the GDI rendering implementation in the `master` branch and the Direct2D / DirectWrite rendering implementation in the `mod-d2ddw` branch.

## Test Environment and Methodology

To objectively measure terminal rendering throughput, transport jitter and frame-throttling timers (cooldowns) must be controlled.
During normal terminal output, PuTTY's internal update cooldown timer (`update_delay`) coalesces screen updates into 17ms–20ms intervals; simply flooding a socket measures the in-memory VT parser rather than actual rendering cost.

In this benchmark, a local runner streams frames over a named pipe (`-serial \\.\pipe\...`), appending a Device Status Report request (DSR, `\x1b[6n`) to the end of each frame to synchronize completion of `term_out()`.
Immediately upon receiving the DSR response, the runner invokes `SendMessageW(hwnd, WM_SETFOCUS, 0, 0)` to bypass the cooldown timer and force a synchronous `term_update()` / `do_paint()` pass for every single frame.

* **Environment**: Windows 11 ARM64 (Qualcomm Snapdragon, ARMv8 64-bit)
* **Terminal Grid Size**: 140 columns × 45 rows (6,300 cells per screen)
* **Tested Binaries** (all Release builds):
  1. **`master` (GDI)**: Traditional GDI (`ExtTextOutW`, etc.) rendering implementation
  2. **`mod-d2ddw` (Before)**: Direct2D / DirectWrite implementation with offscreen `ID2D1BitmapRenderTarget` differential rendering, prior to pipeline and metric optimizations
  3. **`mod-d2ddw` (After)**: Direct2D / DirectWrite implementation after applying pipeline, fill-rate, and metric optimizations

## Benchmark Workloads

To cover realistic terminal rendering patterns, the benchmark combines four stages calibrated to take approximately one minute in total on the `master` GDI build:

1. **Stage 1 (Forward & Backward Text Scrolling, 70 frames)**: Exercises continuous forward scrolling via bottom-row line feeds (`\r\n`), backward scrolling via top-row Reverse Index (`\x1bM`), and scrollback history navigation (`WM_VSCROLL`). Lines mix 24-bit TrueColour, bold, underline, CJK characters (Kanji, Hiragana, Katakana), and color emoji.
2. **Stage 2 (Waiting Spinners & Progress Bars, 40 frames)**: Simulates multi-task build/deploy status displays where rotating spinner characters (`/`, `-`, `¥`, `|`), colored progress bars, and trailing space padding are repeatedly overwritten via carriage return (`\r`).
3. **Stage 3 (Color Terminal TUI & Multi-Attribute Text, 20 frames)**: Simulates full-screen TUI dashboards (`htop`, `btop`, syntax-highlighted editors) where every cell across the 140×45 grid changes 24-bit TrueColour foreground/background, bold, underline, strikethrough, CJK glyphs, and color emoji (`🚀`) on every frame.
4. **Stage 4 (Sparse Status Tables & 256-Color Alternation, 25 frames)**: Alternates between colored status table rows containing wide blank padding and 256-color palette rows to evaluate background fill and space-run efficiency.

## Benchmark Results

### 4-Stage End-to-End Benchmark

The table below shows the elapsed time (including pipe I/O, VT sequence parsing, and rendering) across all four stages:

| Target Binary | Stage 1: Fwd/Bwd Scroll | Stage 2: Spinners `/-¥\|` | Stage 3: Color TUI | Stage 4: Mixed Tables | **Total Time** | **vs. `master`** |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| **`master` (GDI)** | 20.919 s | 12.055 s | 21.901 s | 9.887 s | **64.762 s** | 1.00x (Baseline) |
| **`mod-d2ddw` (Before)** | 6.379 s | 10.434 s | 16.820 s | 8.203 s | **41.836 s** | 1.55x (-35.4%) |
| **`mod-d2ddw` (After)** | **5.132 s** | **10.177 s** | **15.663 s** | **7.820 s** | **38.792 s** | **1.67x (-40.1%)** |

Note that the end-to-end totals above include a fixed transport overhead from the serial backend's 1-byte `ReadFile` mode (`HANDLE_FLAG_UNITBUFFER`).
Even with that transport baseline included, the workload that takes 64.8 seconds on `master` (GDI) completes in 38.8 seconds on `mod-d2ddw` (After)—a 40.1% reduction in total time—with Stage 1 (forward and backward scrolling) improving over 4x from 20.9 seconds down to 5.1 seconds.

### Breakdown of Streaming Periodic Renders vs. Pure Per-Frame Render Time

To isolate pure rendering speed from pipe I/O overhead, the table below breaks down a 10-frame run of the dense Stage 3 Color TUI workload (full 140×45 TrueColour, underline, and CJK updates):

| Target Binary | Stream Ingestion + Periodic 50/60fps Renders (10 frames) | Final Synchronous Render `WM_SETFOCUS` (10 frames) | **Pure Render Time per Frame** | **Render Speedup** |
| :--- | ---: | ---: | ---: | :--- |
| **`master` (GDI)** | 10.102 s | 83 ms | **8.3 ms / frame** | 1.00x (Baseline) |
| **`mod-d2ddw` (Before)** | 9.229 s | 27 ms | **2.7 ms / frame** | 3.07x |
| **`mod-d2ddw` (After)** | **6.630 s (-28.2%)** | **22 ms (-18.5%)** | **2.2 ms / frame** | **3.77x** |

Comparing pure full-screen frame rendering time, optimized `mod-d2ddw` completes a full frame in 2.2 ms compared to 8.3 ms for GDI (3.77x faster).
During stream ingestion—where periodic 60fps updates fire continuously—the optimizations in `mod-d2ddw` (After) reduce elapsed time by 28.2% compared to `mod-d2ddw` (Before) (9.229 s down to 6.630 s).

## Key Optimizations Implemented

The performance and rendering quality improvements in `mod-d2ddw` stem from seven key optimizations:

1. **Differential Rendering via Retained Offscreen `ID2D1BitmapRenderTarget`**: Retains the terminal image across frames in `d2dBRT` and redraws only invalidated cells/runs rather than clearing and repainting the entire screen on every update.
2. **Single-Pass Present via `D2D1_PRIMITIVE_BLEND_COPY`**: Eliminates the full-screen `d2dRT->Clear()` call in `d2d_present()` by copying `d2dBRT` directly onto `d2dRT` using `ID2D1DeviceContext::SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY)`, cutting present fill-rate cost in half.
3. **Single-Pass Background Clearing and Filling**: Skips per-run background clearing on frames where `d2d_clear()` has already cleared the full target, and on differential frames with opaque background colors (`alpha >= 1.0f`), overwrites the cell rectangle in a single `FillRectangle` call without redundant `Clear(0)` or base background passes.
4. **Fast-Path Skipping for Space-Only Runs**: After drawing the background and any underline/preedit lines in `dw_textout()`, runs consisting solely of ASCII spaces (`U+0020`) or ideographic spaces (`U+3000`) immediately pop the axis-aligned clip and return, bypassing `IDWriteTextAnalyzer::GetGlyphs()` and `DrawGlyphRun()`.
5. **Migration to Natural DirectWrite Metrics (`GetMetrics` / `GetDesignGlyphMetrics`) and Rounding Tolerance**: Replaced `GetGdiCompatibleMetrics` and `GetGdiCompatibleGlyphMetrics` (which caused double DPI scaling on high-DPI displays and broke 1:2 halfwidth/fullwidth ratios under `DWRITE_MEASURING_MODE_NATURAL`) with design metrics. Added a `+0.5f` tolerance when comparing floating-point glyph advance against the rounded integer cell width (`width_0 > (FLOAT)width + 0.5f`), preventing false-positive `SetTransform(Scale)` shrinkage and per-glyph batch fragmentation on standard monospace characters.
6. **Color Font Brush Reuse and Removal of Synchronous Debug File I/O**: Replaced per-layer `CreateSolidColorBrush` / `Release` calls inside the COLR color glyph loop with `SetColor()` on a pre-allocated brush (`d2dSCBcol`), and disabled the `D2DBG` synchronous `debug.log` file I/O macro by default.
7. **Guarded URL Hover Hit-Testing**: Restricted `GetCursorPos()` and `ScreenToClient()` calls in `do_text_internal()` to runs that actually carry the `TATTR_URLMASK` attribute.
