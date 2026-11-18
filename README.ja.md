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

## PuTTY CAC Ex のビルドに関する注意

### 必要な環境

* C++ によるデスクトップ開発ワークロードを含む Visual Studio 2022
* upstream PuTTY CAC は 2026 でしたが、手元環境では2026でビルドしています。
* WiX Toolset（MSI ファイルのビルド用） ※未検証
* Windows PowerShell（MSI / ZIP / ハッシュファイルのビルド用）

### ビルド手順

* `packager\build.cmd` を実行してビルドファイルを生成します
* `build` 配下に Visual Studio ソリューションファイルが生成されます
