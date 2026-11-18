# Changelog (`mod-ranvis`)

## Overview
`putty-cac` (PuTTY 0.85 ベース / `e950433`) の `mod-ranvis` ブランチに、`ranvis-puty` (`ranvis2` ブランチ、`a3cce3a4..17868b58`) の拡張パッチを統合した。

## Changes
- **ranvis-puty (`ranvis2`) 機能の統合**:
  - ISO-2022 エンコーディング・自動判別対応 (`utils/iso2022.c`, `utils/iso2022.h`, `utils/iso2022_wc.c`, `stubs/no-iso2022.c`)
  - ローカライズ (`windows/winl10n.c`, `windows/winl10n.h`, `windows/lang/0411/*.lng`, `stubs/nol10n.c`)
  - INI ファイル設定保存 (`windows/ini.c`, `windows/ini.h`, `windows/putty_sample.ini`, `stubs/no-ini.c`)
  - 背景壁紙・半透明・ウィンドウ拡張 (`windows/winwallp.c`, `windows/winwallp.h`, `windows/wtrans.c`, `windows/wtrans.h`, `windows/controls-lazy.h`, `windows/config-sample-text.h`)
  - SFTP 転送帯域制限 (`ssh/xferlimit.c`, `ssh/xferlimit.h`)、IME 状態別カーソル色設定、確認ダイアログ・フォント・UI 改善等
- **PuTTY-CAC 機能とのマージ調整**:
  - `CMakeLists.txt`, `windows/CMakeLists.txt`, `LICENCE`, `settings.c`, `utils/buildinfo.c`, `windows/installer.wxs`, `windows/pageant.c`, `windows/pageant.rc` のコンフリクトを解消し、`#ifdef PUTTY_CAC`（CAC/CAPI/PKCS/FIDO/PuTTYtab 等）と `ranvis-puty` 拡張を共存させた。
- **C++ / Win32 互換性修正**:
  - `code/windows/utils/get_module_filename.c`: `platform.h` で宣言されていた `safeGetModuleFileNameW` を実装。
  - `code/putty.h`: `iso2022_*` プロトタイプの仮引数名 `this` を `data` に変更（C++ 予約語衝突回避）。
  - `code/windows/tab/terminaltext.h`: `<commdlg.h>` を事前インクルードし、`platform.h` 読み込み後に Win32/CRT 再定義マクロ（`GetModuleFileName*`, `CreateWindowEx*`, `MessageBoxA`, `sprintf` 等）を `#undef` して `puttytab` の非 `utils` リンク構成と両立。
- **ビルド構成・履歴**:
  - `.agent_history.md` を作成し、`puttyimp.exe`（`cbor.lib` LTCG バージョン不一致）を YAGNI 原則に基づきビルド対象外とする ADR およびビルド知見を記録。

## 0.85rv20260820tsf2026111801 (2026-11-18)
- **TSF (Text Services Framework) IME 対応とインラインプリエディット**:
  - `code/windows/tsf.h`, `code/windows/tsf.cpp` を追加し、`TsfHostCallbacks` を介した TSF テキストストア・キーフィルタ・候補ウィンドウ位置通知を実装（`CMakeLists.txt` の `LANGUAGES C CXX` 化、`windows/CMakeLists.txt` への `tsf` OBJECT ライブラリ追加、`UseTSF` 設定項目および日本語訳追加）。
  - `code/putty.h`, `code/terminal/terminal.c`: `ATTR_PREEDIT_DOT` / `ATTR_PREEDIT_SOLID` / `ATTR_PREEDIT_TARGET` と `term_set_preedit_text_w()` を追加し、`do_paint()` の空プリエディット・右端 `UCSWIDE` 境界処理を修正。
  - `code/windows/win-gui-seat.h`, `code/windows/window.c`: `WinGuiSeat::tsf` 追加、`tsf_host_*` コールバック・メッセージハンドラ統合、`do_text_internal()` での GDI プリエディット下線描画（点線・実線・太線）を実装。
  - `code/test/test_terminal.c`: `test_preedit` 単体テストとモック実装を追加。
- **IME / ローカライズ / 高 DPI 改善**:
  - `code/windows/window.c`: `VK_PROCESSKEY` / `VK_PACKET` 再送時の上位スキャンコード保持（`~(LPARAM)0x2000`）と `GetMessageTime()` / `GetMessagePos()` 補完、サロゲートペア処理共通化、`wndproc_document_feed()` の範囲・長さ計算修正。
  - `code/windows/winl10n.c`: リネーム実行時対策として `appname` へフォールバックする `find_lng_for_name()` を追加。
  - `code/windows/controls.c`: `ChooseFont` の DPI 取得を `GetDpiForWindow` / `GetDpiForSystem` 対応へ更新。
  - `code/version.h`: バージョンを `0.85rv20260820tsf2026111801` に更新。
- **ドキュメント更新 (`README.md`, `README.ja.md`)**:
  - `README.ja.md` を新規作成し、`README.md` と合わせて PuTTY CAC Ex（`mod-ranvis` / `mod-d2ddw` ブランチ構成、ライセンス、動作要件、ビルド手順）向けに内容を刷新。
- **バージョン更新 (`0.85.3+rv20260820.p1`)**:
  - `code/version.h`: `TEXTVER` を `Release 0.85.3+rv20260820.p1`、`BINARY_VERSION` を `0,85,3,1` に更新（`SSHVER` は upstream PuTTY CAC 準拠の `-Release-0.85-3` を維持）。
