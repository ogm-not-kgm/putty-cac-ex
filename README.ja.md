# PuTTY CAC Ex

PuTTY CAC Ex は、人気の高い、PuTTY フォークである Windows Certificate API や FIDO認証器などを使用したSSH公開鍵認証をサポートする PUTTY CAC のフォークである。

PuTTY CAC Ex は、以下、2つの branch を開発する

* PuTTY CAC + Ranvis ; https://www.ranvis.com/putty (Kentaro Sato, Ranvis.) で公開されている、ごった煮版 (日本語UI、ISO 2022対応、設定のINIファイル格納、壁紙他) の他、主に操作性を変更するパッチを適用したものです。
* PuTTY CAC + Ranvis + D2DDW ; 上記 Ranvisバージョンをベースに https://ice.hotmint.com/putty/d2ddw.html のパッチを適用し、Direct2D + DirectWrite 化されたもの。

ライセンスは未整理ですが、 Ranvis ページ https://github.com/ranvis/putty/blob/ranvis2/LICENCE と、 D2DDW ページに PuTTYライセンスに従うとあるため、MITライセンスとなっており、当配布物も変更差分はMITライセンスで配布します。

## 動作要件

* Microsoft Windows 11 以降 （Windows10でも動作するはずですが、未検証です）
* CAPI, PKCS, FIDOサポートは upstream である Putty CAC に準拠します。

## 使い方

基本的な使用方法については、米国政府 ID Management ウェブサイトの「SSH Using PuTTY-CAC」セクションを参照してください:

[https://www.idmanagement.gov/implement/scl-ssh](https://www.idmanagement.gov/implement/scl-ssh/)

タブ付きセッション（Tabbed Sessions）・コマンドラインでの使用方法も同様です。

* `puttytab.exe` は `putty.exe` にだけ使います。`putty-d2ddw.exe` に対しては使えません（対応する `puttytab` は同梱しません）。
* 配布 ZIP 内の `puttytel.exe`、`pterm.exe`、`pageant.exe` は `master` のビルドであり、Direct2D / DirectWrite 版ではありません。

## PuTTY CAC Ex のビルドに関する注意

### 必要な環境

* C++ によるデスクトップ開発ワークロード（x64 および ARM64 用ビルドツール）を含む Visual Studio 2022
* upstream PuTTY CAC は 2026 でしたが、手元環境では2026でビルドしています。
* Windows PowerShell（ZIP / ハッシュファイルのビルド用）

### ビルド手順

* `packager\build.cmd` を実行すると、`master` と `mod-d2ddw` の一時 Git ワークツリーから `x64` と `arm64` のリリースバイナリをビルドし、`binaries\` 配下に `putty-cac-ex-0.85.3-exYYYYMMDD-<arch>.zip` および `putty-cac-ex-0.85.3-exYYYYMMDD-hash.txt` を生成します（署名と MSI は生成しません）
* 今回のソリューションファイルは `build\release-bins\` 配下（`master-<arch>` / `d2ddw-<arch>`）に生成され、既存の `build\putty.sln` は消えません

## アーキテクチャ決定記録（ADR）

### 共通（`master` / `mod-d2ddw`）

* **ブランチ構成と配布方式**
  * upstream（`NoMoreFood/putty-cac`）追従用の `upstream-master`、Ranvis 統合版の `master`、Direct2D/DirectWrite 拡張版の `mod-d2ddw` の 3 ブランチ構成とします。
  * 配布時は `master` 系の `putty.exe` と `mod-d2ddw` 系の `putty-d2ddw.exe` を 1 つの ZIP に同梱する方針です。
* **バージョニングと `SSHVER` の維持**
  * 正規リリース表記として SemVer ビルドメタデータ形式（`0.85.3+...`）と `BINARY_VERSION 0,85,3,1` を採用しつつ、SSH サーバー側の互換性のため `SSHVER` は upstream PuTTY CAC と同じ `-Release-0.85-3` を維持します。
* **`puttytab` との構造体レイアウト互換維持**
  * `puttytab` が `ReadProcessMemory` で `WinGuiSeat` 構造体を直接参照するため、新規フィールドは `WinGuiSeat` 末尾に追加し、既存メンバ（`und_mode` 等）のオフセットを変更しません。
* **TSF IME と IMM32 フォールバック**
  * `tsf.cpp` をコールバック構造体による疎結合 `OBJECT` ライブラリとして組み込み、既定で TSF によるインライン変換を有効とします。設定無効時や TSF 初期化失敗時は IMM32 へ自動フォールバックします。
* **ビルド対象と依存関係の方針**
  * `libfido2` の LTCG バージョン不一致が解消するまでの一時措置として `puttyimp.exe` は除外しており、LTCG の不一致が解消したら `puttyimp.exe` をビルド対象に戻します。また、`putty` 本体がリンクしない `comctl32.dll` 等の API は動的解決で呼び出します。
  * リリース生成コマンドは `packager\build.cmd`、対象アーキテクチャは `x64` と `arm64` とし、署名および MSI 生成は行わず、`binaries\putty-cac-ex-0.85.3-exYYYYMMDD-<arch>.zip` を生成します。
  * 配布 ZIP に同梱する `puttytab.exe` は `master` の `putty.exe` 専用であり、`putty-d2ddw.exe` には使えません（対応する `puttytab` は同梱しません）。また、`puttytel.exe`、`pterm.exe`、`pageant.exe` は `master` の GDI ビルドであり、Direct2D / DirectWrite 版ではありません。

### `mod-d2ddw` 固有（Direct2D / DirectWrite 拡張）

* **GDI 端末描画の撤廃と DirectWrite ネイティブ化**
  * 端末描画およびリサイズ時のサイズチップ（`sizetip.cpp`）から GDI 依存を撤廃し、`IDWriteFactory::GetSystemFontCollection` によるネイティブフォント取得（安全なフォールバック付き）と `CF_TTONLY` ダイアログ制限へ移行しています。
* **保持型オフスクリーンバッファによる差分描画と自然メトリクス統一**
  * `ID2D1BitmapRenderTarget`（`d2dBRT`）に描画結果を保持して `ID2D1DeviceContext`（`D2D1_PRIMITIVE_BLEND_COPY`）で 1 パス転送し、変更セル／ラン単位のクリップ・背景クリア省略・空白ラン描画スキップを行うことで、透過・ガラス効果・壁紙合成と両立しつつ全画面再描画を回避します。
  * `DWRITE_MEASURING_MODE_NATURAL` 描画に合わせてフォント・グリフ幅計測を `GetMetrics` / `GetDesignGlyphMetrics`（丸め許容誤差 `+0.5f` 付き）へ統一し、不要な横縮小変換と 1 文字分割描画を排除しています（パフォーマンス測定結果は [benchmark.ja.md](benchmark.ja.md) を参照）。
* **タイトルバー フォントサイズ調整 UI ＆ フォントスポイト**
  * `fontzoom.cpp` を `FontZoomCallbacks` による疎結合 `OBJECT` ライブラリとして構成し、タイトルバー右上のズーム UI・`Ctrl + ホイール` プレビュー・別 PuTTY ウィンドウからの `WM_COPYDATA` フォント取得を実装しています。
