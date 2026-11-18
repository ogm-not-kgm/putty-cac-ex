# Changelog (`mod-d2ddw`)

## Overview
`putty-cac` (`master` / PuTTY 0.85 ベース `e950433`) から新規作成した `mod-d2ddw` ブランチに、`putty-d2ddw/patch/d2ddw.diff`（Direct2D / DirectWrite 描画対応等）を統合した。

## Changes
- **`d2ddw.diff` 機能の統合**:
  - Direct2D / DirectWrite 描画実装 (`windows/d2ddw.cpp`, `windows/d2ddw.h`, `windows/window.c`, `cmake/platforms/windows.cmake`)
  - ハイパーリンク表示対応 (`terminal/terminal.c`, `terminal/terminal.h`, `putty.h`)
  - ISO-2022 対応 (`iso2022.c`, `iso2022.h`)、ローカライズ (`windows/winl10n.c`, `windows/*.lng` Shift-JIS, `unix/uxl10n.c`)、INI 設定保存・背景/半透明関連拡張 (`windows/storage.c`, `windows/pageant.c`, `windows/config.c` 等)
  - ADB バックエンド追加 (`otherbackends/adb.c`, `otherbackends/CMakeLists.txt`, `be_list.c`)
  - ※ `putty-cac` に存在しない `doc/licence.but` の変更は除外。
- **PuTTY-CAC / PuTTY 0.85 とのマージ・互換性修正**:
  - `CMakeLists.txt`, `putty.h`, `terminal/terminal.c`, `windows/dialog.c`, `windows/pageant.c`, `windows/platform.h`, `windows/window.c` のコンフリクトを解消し、`#ifdef PUTTY_CAC`（CAC/CAPI/PKCS/FIDO/PuTTYtab 等）と `d2ddw` 拡張を共存させた。
  - `code/putty.h`: PuTTY 0.85 の `ATTR_ACTCURS` / `ATTR_PASCURS` / `ATTR_RIGHTCURS` を維持しつつ、`TDATTR_MASK` を `0x80000000ULL` として `TATTR_URLMASK` (64bit) と両立。
  - `code/windows/window.c`: `wintw_draw_cursor` の `ATTR_PASCURS` / `ATTR_ACTCURS` に D2D 描画および `#ifndef D2D` 分岐を適用し、`IMR_DOCUMENTFEED` の廃止メンバー `dispcursx` / `dispcursy` を `curs.x` / `curs.y - disptop`（範囲ガード付き）へ更新。
  - `code/windows/tab/terminaltext.h`: `<commdlg.h>` を事前インクルードし、`platform.h` 読み込み後に l10n/Win32 再定義マクロ（`MessageBoxA`, `CreateWindowExA`, `DialogBoxParamA`, `CreateDialogParamA`, `sprintf`, `_vsnprintf`, `vsnprintf`, `GetOpenFileNameA`, `GetSaveFileNameA`）を `#undef` して `puttytab` (C++) との干渉を解消。
- **履歴・ADR**:
  - `mod-ranvis` から `.agent_history.md` を取り込み、`d2ddw.diff` 統合時の知見を追記。

## `mod-ranvis` (`ranvis2`) 未適用機能の統合
- **UI・色設定拡張**:
  - カラー設定のサンプルテキストプレビュー (`windows/config-sample-text.h`)、HSL 色空間ユーティリティ (`utils/colourspace.h`)、遅延初期化コントロール (`windows/controls-lazy.h`)、および関連ダイアログ・コントロール拡張 (`dialog.c`, `windows/controls.c`, `windows/dialog.c`, `windows/putty-common.rc2`, `windows/puttygen.c`, `windows/puttygen.rc`) を統合。
- **Pageant / PSCP / PSFTP / SSH**:
  - Pageant の `-reencrypt_on` / `--reencrypt-on`、リクエスト確認メニュー (`IDM_CONFIRM_REQUEST`)、確認遅延設定 (`windows/pageant.c`, `pageant.c`, `pageant.h`) を統合し、`PUTTY_CAC` 固有メニュー ID (`0x0100`..`0x01A0`) と共存。
  - PSCP / PSFTP の転送帯域制限 (`ssh/xferlimit.c`, `ssh/xferlimit.h`, `pscp.c`, `psftp.c`, `ssh/sftp.c`)、`ssh_connection_sharing_ask`、`no_dbackspace` 3状態化、`query_end` を統合。
- **INI 管理近代化とストレージ統合**:
  - `windows/ini.c` / `windows/ini.h` / `stubs/no-ini.c` / `windows/utils/app_conf.c` を導入し、`-ini` / `-no-ini` オプション・JumpList・Host CA の INI 読み書きを `windows/storage.c` / `windows/pageant.c` / `windows/plink.c` / `windows/sftp.c` / `windows/pterm.c` / `windows/putty.c` に統合（`putty_sample.ini` 更新、`d2ddw` 固有 `pvkey_str` 等の設定を保持）。
- **ローカライズ (UTF-16LE BOM) と ISO-2022 分離**:
  - 旧 Shift-JIS 形式の `code/windows/*.lng` および旧 `code/iso2022.{c,h}` を削除。
  - UTF-16LE BOM + CRLF 形式の `windows/winl10n.c` / `winl10n.h` / `windows/lang/0411/*.lng` を導入し、`code/windows/lang/0411/putty.lng` に `d2ddw` / ADB / Hyperlink 固有の 76 キーを統合。
  - `utils/iso2022.c` / `utils/iso2022.h` / `utils/iso2022_wc.c` / `stubs/no-iso2022.c` へ分離し、`terminal/terminal.c`・`ldisc.c`・`windows/unicode.c` を更新。
- **ウィンドウ半透明・壁紙・IME・タスク切替・競合解消**:
  - `windows/wtrans.{c,h}`（ウィンドウ透過）、`windows/winwallp.{c,h}`（壁紙）、`switch_skip_min`（Ctrl+Tab で最小化ウィンドウをスキップ）、`query_end`、`wndproc_document_feed` を `windows/window.c` に統合。
  - 3-way マージに伴う `conf.h` 重複 `CONF_OPTION` 整理、`putty.h` / `windows/platform.h` の旧宣言整理、`windows/CMakeLists.txt` の `winl10n.c` 重複およびリンク順修正、`window.c` 内の `wtrans_activate` 配置・`general_textout2`/`dw_general_textout2`・`ExtTextOutW2` 重複・コメント入れ子・`close_session(wgs)`/`logevent(wgs->logctx, ...)`、`tab/terminaltext.h` の `#undef` を修正し、`d2ddw` と `PUTTY_CAC` の全機能を共存。
  - `.agent_history.md` に 3-way マージ時の知見を 1 行追記。
- **クリーンアップ・初期化/IME修正・バージョン更新 (`0.85.3.0p2026111701`)**:
  - `code/version.h`: バージョン表記を `0.85.3.0p2026111701` へ更新。
  - `code/windows/unicode.c`: `cp_list` 内の重複していた 13 エントリ（`UTF-8 (CJK)` 等）を削除。
  - `code/terminal/terminal.c`: `term_input_data_from_unicode` 末尾の重複 `iso2022_settranschar(..., 1)` および `term_translate` 先頭のシャドウイングされていた不要な `iso2022` 初期化を削除。
  - `code/windows/window.c`: `WinMain` で `d2d_init` / `init_fonts` を `term_init` の前へ移動して `ucsdata` を事前初期化し、`VK_PROCESSKEY`/`VK_PACKET` の `TranslateMessage` 呼び出し（`~(LPARAM)0x2000` と `MSG` 初期化）・`wndproc_document_feed`（`term_dispcursy` 範囲ガードおよび `dwTargetStrLen`）・`WM_KEYUP` 処理（本家 PuTTY 0.85 準拠）を整備。
  - `code/windows/winl10n.c`: リネームされた実行ファイルでも `appname` ベース（`PuTTY.lng` 等）へフォールバックするよう `get_lng_file_path()` を修正。
- **TSF (Text Services Framework) サポート導入・インライン変換対応 (`0.85.3.0p2026111702`)**:
  - `code/version.h`: バージョン表記を `0.85.3.0p2026111702` へ更新。
  - `code/conf.h`, `code/windows/config.c`, `code/windows/lang/0411/putty.lng`: `CONF_use_tsf`（既定 `true`）・Window/Behaviour チェックボックス・日本語訳を追加し、設定または TSF 初期化失敗時は IMM32 へ自動フォールバック。
  - `code/windows/tsf.h`, `code/windows/tsf.cpp`, `code/windows/window.c`, `code/windows/win-gui-seat.h`, `code/windows/CMakeLists.txt`: `ITfThreadMgr` / `ITextStoreACP` / `ITfContextOwnerCompositionSink` / `ITfTextEditSink` / `ITfCompartmentEventSink` / `ITfKeystrokeMgr` / `ITfDisplayAttributeMgr` を実装し、IME ON/OFF 同期・部分確定 (`ShiftStart`)・`GetTextExt` 候補窓追従・`VK_PROCESSKEY`/`VK_PACKET` 共存・`WM_IME_*` 排他制御・`puttytab` の `WinGuiSeat` レイアウト整合を維持。
  - `code/putty.h`, `code/terminal/terminal.c`, `code/windows/d2ddw.cpp`, `code/test/test_terminal.c`: `term_set_preedit_text_w` と `ATTR_PREEDIT_MASK`（点線/実線/太実線下線）による Direct2D インライン未確定描画、`preedit` 空文字・右端クランプ・`UCSWIDE` 境界ガード、および単体テスト `test_preedit` を追加。
- **GDI 描画コードからの完全脱却・ARM64 ビルド対応 (`0.85.3.0p2026111703`)**:
  - `code/version.h`: バージョン表記を `0.85.3.0p2026111703` へ更新。
  - `code/windows/d2ddw.cpp`, `code/windows/d2ddw.h`, `code/windows/window.c`: `dw_fontface` の `IDWriteGdiInterop::CreateFontFromLOGFONT` 依存を廃止し、`IDWriteFactory::GetSystemFontCollection` による DirectWrite ネイティブ取得（ローカライズ名およびフォールバック対応）と `dw_init_with_metrics` へ移行。取り消し線・手動下線 (`d2d_draw_hline_fg`)、文字幅判定 (`dw_char_width`)、および `WM_PAINT` 再描画を D2D/DW 化し、`icon_create` の WIC リーク修正・8-bit パレット処理整理・IMM32 用 `lfontw` グローバル廃止を実施。
  - `code/windows/controls.c`, `code/windows/sizetip.cpp` (`sizetip.c` 置換), `code/windows/CMakeLists.txt`: フォント選択ダイアログに `CF_TTONLY` と `GetDpiForWindow`/`GetDpiForSystem` を適用し、リサイズ時のサイズチップを Direct2D/DirectWrite 自己完結実装 (`sizetip.cpp`) へ刷新。
  - `code/windows/window.c`, `code/windows/win-gui-seat.h`, `code/windows/winwallp.h` (`winwallp.c` 削除), `code/cmake/platforms/windows.cmake`: `#ifdef D2D` / `#ifndef D2D`、旧 GDI 描画・壁紙 (`xtrans_*` / `bg_WndProc` / `ExtTextOutW2`) および `WinGuiSeat` の GDI フィールド、`/DD2D` マクロを削除し、RTF コピーのカラーテーブル生成を `wgs->colours[]` 参照へ変更。
  - `code/windows/tsf.cpp`: `#include <atlbase.h>` 依存を自前 `CComPtr` テンプレートに置き換え、`atls.lib` 未導入の ARM64 Windows 環境でのビルドに対応。
- **ATL (`<atlbase.h>`) 復元 (`0.85.3.0p2026111704`)**:
  - `code/version.h`: バージョン表記を `0.85.3.0p2026111704` へ更新。
  - `code/windows/tsf.cpp`: ARM64 用 `atls.lib` 導入に伴い、自前 `CComPtr` 互換コードを削除して標準の `<atlbase.h>` に復元。
  - `.agent_history.md`: ARM64 用 ATL 導入済み環境に関する記録を更新。
- **タイトルバー フォントサイズ調整 UI ＆ `Ctrl + ホイール` プレビュー**:
  - `code/windows/fontzoom.h`, `code/windows/fontzoom.cpp`, `code/windows/CMakeLists.txt`: タイトルバー右上（最小化ボタンの左）に追従オーバーレイ（`pt` 入力ボックス＋スピンボタン）を配置し、フォーカス時の `Ctrl + +` / `-` / `0` / `↑↓` / `Enter` / `Esc` 操作および最大化時の無効化を実装。
  - `code/windows/fontzoom.cpp`, `code/windows/d2ddw.cpp`, `code/windows/d2ddw.h`: ホバー時の `Ctrl + マウスホイール` で OS 選択枠風の半透明プレビュー枠を表示し、`Ctrl` キー解放時に行・列数を維持したままフォント変更とウィンドウリサイズを確定する機構（および描画用フォントを破壊しないセル計測 `dw_measure_cell_for_pt`）を追加。
- **ドラッグ型フォントスポイト機能 ＆ `Duplicate Session` フォントズーム引き継ぎ**:
  - `code/windows/fontzoom.h`, `code/windows/fontzoom.cpp`: タイトルバー右上のフォントサイズ入力ボックスの左隣にドラッグ型スポイトボタン（`PuTTYFontPickBtn`）を追加し、同クラスの別 PuTTY ウィンドウ上での青枠ハイライトとドロップによるフォント取得を実装。
  - `code/windows/window.c`, `code/windows/win-gui-seat.h`: `RegisterWindowMessage` と `WM_COPYDATA` により、同一バージョン間で `CONF_font` / `CONF_widefont` / `CONF_altfont` および D2D/DirectWrite 拡張項目（`font_quality`, `rendering_mode`, `gamma`, `enhanced_contrast`, `cleartype_level`, `line_gap`, `baseline_offset`, `baseline_offset_fw`, `bold_style`, `font_zoom_delta`）を吸い出して即時反映する処理を実装。また、`IDM_DUPSESS` の共有メモリ引き継ぎに `font_zoom_delta` を含め、`Duplicate Session` 時も現在のフォントサイズを引き継ぐよう修正。
  - `code/windows/sizetip.cpp`, `code/windows/platform.h`, `code/windows/window.c`, `code/windows/win-gui-seat.h`: サイズ変更時の自動消去付きフォントサイズツールチップ（`ShowFontTip` / `HideFontTip`）と `WinGuiSeat` 末尾への `font_zoom_delta` / `font_zoom` 統合を追加。
- **Direct2D / DirectWrite 差分描画・パフォーマンス改善・透過時カーソル色修正**:
  - `code/windows/d2ddw.cpp`, `code/windows/d2ddw.h`, `code/windows/window.c`: オフスクリーン保持用 `ID2D1BitmapRenderTarget`（`d2dBRT`）を導入し、毎フレームの全画面クリア・全セル `ATTR_INVALID` 化を撤廃して変更セル／ランのみの差分描画へ移行（リサイズ・壁紙/パレット/フォント/フォーカス透過率変更・`WM_DISPLAYCHANGE` 時のみ `d2d_invalidate()` による全画面再描画を実施、`WM_PAINT` 再露出時は保持ビットマップを `d2d_present()` で 1 回 `DrawBitmap` 転送）。
  - `code/windows/d2ddw.cpp`, `code/windows/window.c`: `dw_textout()` でサブラン矩形へ `PushAxisAlignedClip` 適用、結合文字（`TATTR_COMBINING`）を除くセル／ラン単位の透過背景クリア＋`wp_draw_impl()`（角丸なし）＋`ETO_OPAQUE` 塗りつぶし（`d2dSCBbg` 退避復元付き）、`BG_FILE_DESKTOP` オフセット修正、アクティブブロックカーソルへの `ALPHA_CURSOR` 適用、カーソル描画境界補正（1px はみ出し防止）、URL ホバー時の差分 `ATTR_INVALID` 化を実装。
  - `code/windows/d2ddw.cpp`: `dw_textout()` の `wlen = 256` 毎フレームリセットによる配列再確保および `GetGlyphs` との容量不整合を修正し、カラー絵文字描画の `IDWriteColorGlyphRunEnumerator1` COM リークを解消。
- **不要 GDI 由来コード・デッドコード削除**:
  - `code/windows/window.c`: `do_text_internal()` から未使用の `nfont`（`switch (lattr)` / `ATTR_NARROW` / `CSET_OEMCP` 分岐含む）および `xoffset` を削除し、`init_fonts()` の `wgs->und_mode = UND_FONT` 代入と `wgs->und_mode == UND_LINE` の死に分岐を削除（`VT_POORMAN` 時の `lattr = LATTR_WIDE` は維持）。
  - `code/windows/win-gui-seat.h`, `code/windows/d2ddw.h`: 未使用の `FONT_*` マクロ・コメントアウトされた旧宣言群・未参照の `extern int offset_width, offset_height;` を削除（`puttytab` の構造体レイアウト互換のため `enum UnderlineMode` と `WinGuiSeat.und_mode` は維持）。
  - `code/windows/d2ddw.cpp`: `d2d_clear()` 内の `return;` 以降の到達不能な旧ボーダー／余白描画コードおよびファイル先頭のコメントアウトされた旧宣言群を削除。
- **ドキュメント更新 (`README.md`, `README.ja.md`)**:
  - `README.ja.md` を新規作成し、`README.md` と合わせて PuTTY CAC Ex（`mod-ranvis` / `mod-d2ddw` ブランチ構成、ライセンス、動作要件、ビルド手順）向けに内容を刷新。
- **バージョン更新 (`0.85.3+rv20260820.d2ddw20250211.p1`)**:
  - `code/version.h`: `TEXTVER` を `Release 0.85.3+rv20260820.d2ddw20250211.p1`、`BINARY_VERSION` を `0,85,3,1` に更新（`SSHVER` は upstream PuTTY CAC 準拠の `-Release-0.85-3` を維持）。
