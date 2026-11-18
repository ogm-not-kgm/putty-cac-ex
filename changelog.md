# Changelog (`master` / `mod-ranvis`)

## Overview
Integrated the `ranvis-puty` (`ranvis2` branch, `a3cce3a4..17868b58`) extension patches into `putty-cac` (PuTTY 0.85 base / `e950433`).

## Changes
- **Integration of `ranvis-puty` (`ranvis2`) features**:
  - ISO-2022 encoding and auto-detection support (`utils/iso2022.c`, `utils/iso2022.h`, `utils/iso2022_wc.c`, `stubs/no-iso2022.c`)
  - Localization (`windows/winl10n.c`, `windows/winl10n.h`, `windows/lang/0411/*.lng`, `stubs/nol10n.c`)
  - INI file configuration storage (`windows/ini.c`, `windows/ini.h`, `windows/putty_sample.ini`, `stubs/no-ini.c`)
  - Wallpaper, window transparency, and window/UI extensions (`windows/winwallp.c`, `windows/winwallp.h`, `windows/wtrans.c`, `windows/wtrans.h`, `windows/controls-lazy.h`, `windows/config-sample-text.h`)
  - SFTP bandwidth limiting (`ssh/xferlimit.c`, `ssh/xferlimit.h`), IME-state cursor color settings, confirmation dialogs, and font/UI improvements.
- **PuTTY-CAC compatibility & merge adjustments**:
  - Resolved conflicts in `CMakeLists.txt`, `windows/CMakeLists.txt`, `LICENCE`, `settings.c`, `utils/buildinfo.c`, `windows/installer.wxs`, `windows/pageant.c`, and `windows/pageant.rc` so `#ifdef PUTTY_CAC` (CAC/CAPI/PKCS/FIDO/PuTTYtab, etc.) coexists with `ranvis-puty` extensions.
- **C++ / Win32 compatibility fixes**:
  - `code/windows/utils/get_module_filename.c`: Implemented `safeGetModuleFileNameW` declared in `platform.h`.
  - `code/putty.h`: Renamed parameter `this` to `data` in `iso2022_*` prototypes to avoid C++ keyword collision.
  - `code/windows/tab/terminaltext.h`: Pre-included `<commdlg.h>` and `#undef`'d Win32/CRT redefinition macros (`GetModuleFileName*`, `CreateWindowEx*`, `MessageBoxA`, `sprintf`, etc.) after `platform.h` to stay compatible with `puttytab`'s non-`utils` link configuration.
- **Build configuration & history**:
  - Excluded `puttyimp.exe` from the build due to `cbor.lib` LTCG version mismatch (recorded in `.agent_history.md`).

## `0.85.3+rv20260820.p1` (2026-11-18)
- **TSF (Text Services Framework) IME support & inline preedit**:
  - Added `code/windows/tsf.h` and `code/windows/tsf.cpp`, implementing TSF text store, keystroke filtering, and candidate window positioning via `TsfHostCallbacks` (switched `CMakeLists.txt` to `LANGUAGES C CXX`, added `tsf` OBJECT library to `windows/CMakeLists.txt`, and added the `UseTSF` option with Japanese translation).
  - `code/putty.h`, `code/terminal/terminal.c`: Added `ATTR_PREEDIT_DOT` / `ATTR_PREEDIT_SOLID` / `ATTR_PREEDIT_TARGET` and `term_set_preedit_text_w()`, and fixed empty preedit and right-edge `UCSWIDE` boundary handling in `do_paint()`.
  - `code/windows/win-gui-seat.h`, `code/windows/window.c`: Added `WinGuiSeat::tsf`, `tsf_host_*` callbacks/message handlers, and GDI inline preedit underline rendering (dotted/solid/thick) in `do_text_internal()`.
  - `code/test/test_terminal.c`: Added `test_preedit` unit test and mock implementations.
- **IME / Localization / High-DPI improvements**:
  - `code/windows/window.c`: Preserved upper scan code bits (`~(LPARAM)0x2000`) and populated `GetMessageTime()` / `GetMessagePos()` on `VK_PROCESSKEY` / `VK_PACKET` re-dispatch, unified surrogate pair handling, and fixed bounds/length calculation in `wndproc_document_feed()`.
  - `code/windows/winl10n.c`: Added `find_lng_for_name()` to fall back to `appname` (e.g., `PuTTY.lng`) when the executable is renamed.
  - `code/windows/controls.c`: Updated `ChooseFont` DPI detection to use `GetDpiForWindow` / `GetDpiForSystem`.
- **Documentation & Licensing (`README.md`, `README.ja.md`, `code/LICENCE`)**:
  - Added `README.ja.md` and updated `README.md` for PuTTY CAC Ex (`mod-ranvis` / `mod-d2ddw` branch structure, licensing, prerequisites, and build instructions).
  - Updated `code/LICENCE` with `Hironori OGUMA`.
- **Version update (`0.85.3+rv20260820.p1`)**:
  - `code/version.h`: Updated `TEXTVER` to `Release 0.85.3+rv20260820.p1` and `BINARY_VERSION` to `0,85,3,1`, while keeping `SSHVER` as `-Release-0.85-3` (matching upstream PuTTY CAC).
