# Changelog (`mod-d2ddw`)

## Overview
Integrated `putty-d2ddw/patch/d2ddw.diff` (Direct2D / DirectWrite rendering and related extensions) onto the `mod-d2ddw` branch based on `putty-cac` (`master` / PuTTY 0.85 base `e950433`) and `mod-ranvis`.

## Changes
- **Integration of `d2ddw.diff` features**:
  - Direct2D / DirectWrite rendering implementation (`windows/d2ddw.cpp`, `windows/d2ddw.h`, `windows/window.c`, `cmake/platforms/windows.cmake`)
  - Hyperlink display support (`terminal/terminal.c`, `terminal/terminal.h`, `putty.h`)
  - ISO-2022 support (`iso2022.c`, `iso2022.h`), localization (`windows/winl10n.c`, `windows/*.lng`, `unix/uxl10n.c`), INI configuration storage, and background/transparency extensions (`windows/storage.c`, `windows/pageant.c`, `windows/config.c`, etc.)
  - ADB backend (`otherbackends/adb.c`, `otherbackends/CMakeLists.txt`, `be_list.c`)
  - Note: Changes to `doc/licence.but` (absent in `putty-cac`) were excluded.
- **PuTTY-CAC / PuTTY 0.85 merge & compatibility fixes**:
  - Resolved conflicts across `CMakeLists.txt`, `putty.h`, `terminal/terminal.c`, `windows/dialog.c`, `windows/pageant.c`, `windows/platform.h`, and `windows/window.c` so `#ifdef PUTTY_CAC` (CAC/CAPI/PKCS/FIDO/PuTTYtab, etc.) coexists with `d2ddw` extensions.
  - `code/putty.h`: Preserved PuTTY 0.85 `ATTR_ACTCURS` / `ATTR_PASCURS` / `ATTR_RIGHTCURS` while setting `TDATTR_MASK` to `0x80000000ULL` for 64-bit compatibility with `TATTR_URLMASK`.
  - `code/windows/window.c`: Updated `wintw_draw_cursor` (`ATTR_PASCURS` / `ATTR_ACTCURS`) for D2D rendering and updated `IMR_DOCUMENTFEED` to use `curs.x` / `curs.y - disptop` (with bounds guards) instead of removed `dispcursx` / `dispcursy`.
  - `code/windows/tab/terminaltext.h`: Pre-included `<commdlg.h>` and `#undef`'d Win32/l10n redefinition macros (`MessageBoxA`, `CreateWindowExA`, `DialogBoxParamA`, `CreateDialogParamA`, `sprintf`, `_vsnprintf`, `vsnprintf`, `GetOpenFileNameA`, `GetSaveFileNameA`) after `platform.h` to avoid interference with `puttytab` (C++).
- **Integration of `mod-ranvis` (`ranvis2`) features**:
  - **UI & color settings**: Color sample text preview (`windows/config-sample-text.h`), HSL color space utilities (`utils/colourspace.h`), lazy controls (`windows/controls-lazy.h`), and related dialog/control extensions.
  - **Pageant / PSCP / PSFTP / SSH**: Pageant `-reencrypt_on` / `--reencrypt-on`, request confirmation menu (`IDM_CONFIRM_REQUEST`), confirmation delay settings, PSCP/PSFTP bandwidth limiting (`ssh/xferlimit.{c,h}`), `ssh_connection_sharing_ask`, 3-state `no_dbackspace`, and `query_end`.
  - **Modernized INI management & storage**: `windows/ini.{c,h}`, `stubs/no-ini.c`, `windows/utils/app_conf.c`, `-ini` / `-no-ini` options, JumpList and Host CA INI support (`putty_sample.ini` updated while preserving `d2ddw`-specific keys such as `pvkey_str`).
  - **UTF-16LE BOM localization & separated ISO-2022**: Replaced legacy Shift-JIS `.lng` and old `iso2022.{c,h}` with `windows/winl10n.{c,h}`, `windows/lang/0411/*.lng` (including 76 `d2ddw`/ADB/Hyperlink keys in `putty.lng`), and `utils/iso2022*` / `stubs/no-iso2022.c`.
  - **Window transparency, IME, and 3-way merge cleanup**: Integrated `windows/wtrans.{c,h}`, `switch_skip_min`, `query_end`, `wndproc_document_feed`, and resolved 3-way merge duplications.
- **Initialization / IME / Localization fixes**:
  - `code/windows/unicode.c`: Removed 13 duplicate entries from `cp_list`.
  - `code/terminal/terminal.c`: Removed duplicate `iso2022_settranschar(..., 1)` and shadowed `iso2022` initialization.
  - `code/windows/window.c`: Moved `d2d_init` / `init_fonts` before `term_init` in `WinMain` so `ucsdata` is initialized upfront; fixed `VK_PROCESSKEY`/`VK_PACKET` `TranslateMessage` handling (`~(LPARAM)0x2000` and `MSG` initialization), `wndproc_document_feed` bounds/length, and `WM_KEYUP` handling.
  - `code/windows/winl10n.c`: Updated `get_lng_file_path()` to fall back to `appname` (e.g., `PuTTY.lng`) when the executable is renamed.
- **TSF (Text Services Framework) IME support & inline preedit**:
  - `code/conf.h`, `code/windows/config.c`, `code/windows/lang/0411/putty.lng`: Added `CONF_use_tsf` (default `true`) with automatic fallback to IMM32.
  - `code/windows/tsf.{h,cpp}`, `code/windows/window.c`, `code/windows/win-gui-seat.h`, `code/windows/CMakeLists.txt`: Implemented `ITfThreadMgr` / `ITextStoreACP` / composition & keystroke sinks with IME ON/OFF sync, partial commit (`ShiftStart`), `GetTextExt` candidate positioning, and `puttytab` `WinGuiSeat` layout compatibility.
  - `code/putty.h`, `code/terminal/terminal.c`, `code/windows/d2ddw.cpp`, `code/test/test_terminal.c`: Added `term_set_preedit_text_w`, `ATTR_PREEDIT_MASK` inline underlines, boundary guards, and `test_preedit` unit test.
- **Complete removal of GDI terminal rendering & ARM64 build support**:
  - `code/windows/d2ddw.{cpp,h}`, `code/windows/window.c`: Migrated `dw_fontface` from `IDWriteGdiInterop::CreateFontFromLOGFONT` to `IDWriteFactory::GetSystemFontCollection` (with localized name and safe fallbacks) and `dw_init_with_metrics`; migrated strikethrough/manual underline (`d2d_draw_hline_fg`), `dw_char_width`, and `WM_PAINT` to D2D/DW; fixed WIC leak in `icon_create`.
  - `code/windows/controls.c`, `code/windows/sizetip.cpp`, `code/windows/CMakeLists.txt`: Applied `CF_TTONLY` and `GetDpiForWindow`/`GetDpiForSystem` in `ChooseFont`; replaced `sizetip.c` with self-contained D2D/DW `sizetip.cpp`.
  - `code/windows/window.c`, `code/windows/win-gui-seat.h`, `code/windows/winwallp.h`, `code/cmake/platforms/windows.cmake`: Removed `#ifdef D2D` / `#ifndef D2D`, legacy GDI rendering/wallpaper code, and `/DD2D`.
- **Titlebar font zoom UI, `Ctrl + Wheel` preview, & drag font eyedropper**:
  - `code/windows/fontzoom.{h,cpp}`, `code/windows/CMakeLists.txt`, `code/windows/d2ddw.{cpp,h}`: Added titlebar font-size input/spin overlay (`Ctrl + +/-/0/Up/Down/Enter/Esc`), `Ctrl + MouseWheel` translucent resize preview with `dw_measure_cell_for_pt`, and drag-and-drop font eyedropper (`PuTTYFontPickBtn`) using `RegisterWindowMessage` / `WM_COPYDATA`.
  - `code/windows/window.c`, `code/windows/win-gui-seat.h`, `code/windows/sizetip.cpp`: Added `font_zoom_delta` preservation on `IDM_DUPSESS`, `ShowFontTip` / `HideFontTip`, and `WinGuiSeat` tail integration.
- **Direct2D / DirectWrite partial redraw optimization & transparent cursor color fix**:
  - `code/windows/d2ddw.{cpp,h}`, `code/windows/window.c`: Introduced retained offscreen `ID2D1BitmapRenderTarget` (`d2dBRT`) to eliminate full-screen clears and full-screen `ATTR_INVALID` loops on every update, performing `d2d_invalidate()` only on resize, wallpaper/palette/font/focus-alpha changes, and `WM_DISPLAYCHANGE`, and fast `d2d_present()` (`DrawBitmap`) on `WM_PAINT` expose.
  - `code/windows/d2ddw.{cpp,h}`, `code/windows/window.c`: Added per-run `PushAxisAlignedClip`, non-combining cell/run background clear + `wp_draw_impl(..., false)` + `ETO_OPAQUE` fill (saving/restoring `d2dSCBbg`), `BG_FILE_DESKTOP` origin fix, `ALPHA_CURSOR` for active block cursors, cursor stroke boundary clamping, and differential `ATTR_INVALID` for URL hover.
  - `code/windows/d2ddw.cpp`: Fixed `wlen = 256` per-call buffer reallocation and `GetGlyphs` capacity mismatch, and fixed `IDWriteColorGlyphRunEnumerator1` COM leak.
- **Dead GDI-era code cleanup**:
  - `code/windows/window.c`, `code/windows/win-gui-seat.h`, `code/windows/d2ddw.{h,cpp}`: Removed unused `nfont`, `xoffset`, `und_mode == UND_LINE` branch, unused `FONT_*` macros, unreferenced `offset_width`/`offset_height` externs, commented-out legacy declarations, and unreachable code in `d2d_clear()` (keeping `VT_POORMAN` `lattr = LATTR_WIDE` and `WinGuiSeat.und_mode` for `puttytab` struct compatibility).
- **Documentation, Licensing, & Version (`0.85.3+rv20260820.d2ddw20250211.p1`)**:
  - Added `README.ja.md` and `changelog.ja.md`, and updated `README.md` and `changelog.md` in English.
  - Updated `code/LICENCE` with `iceiv` and `Hironori OGUMA`.
  - `code/version.h`: Updated `TEXTVER` to `Release 0.85.3+rv20260820.d2ddw20250211.p1` and `BINARY_VERSION` to `0,85,3,1`, while keeping `SSHVER` as `-Release-0.85-3` (matching upstream PuTTY CAC).
