# PuTTY CAC Ex

PuTTY CAC Ex is a fork of PuTTY CAC, a popular PuTTY fork that adds support for SSH public key authentication using the Windows Certificate API (CAPI), FIDO authenticators, and more.

PuTTY CAC Ex develops the following two branches:

* PuTTY CAC + Ranvis: Based on the all-in-one build published at https://www.ranvis.com/putty (Kentaro Sato, Ranvis.) featuring Japanese UI, ISO 2022 support, INI file configuration storage, wallpaper, etc., with additional patches primarily aimed at improving usability.
* PuTTY CAC + Ranvis + D2DDW: Based on the Ranvis version above, with the patch from https://ice.hotmint.com/putty/d2ddw.html applied to enable Direct2D + DirectWrite rendering.

Although licensing is not yet fully consolidated, both the Ranvis repository (https://github.com/ranvis/putty/blob/ranvis2/LICENCE) and the D2DDW page state that they follow the PuTTY license (MIT License), so the modifications in this distribution are also distributed under the MIT License.

## Prerequisites

* Microsoft Windows 11 or later (should also work on Windows 10, though untested)
* CAPI, PKCS, and FIDO support follows upstream PuTTY CAC.

## Usage

You can find basic usage instructions on the US Government ID Management website under the "SSH Using PuTTY-CAC" section:

[https://www.idmanagement.gov/implement/scl-ssh](https://www.idmanagement.gov/implement/scl-ssh/)

Tabbed Sessions and command-line usage also follow upstream PuTTY CAC.

* `puttytab.exe` must only be used with `putty.exe`. Do not use it with `putty-d2ddw.exe` (no corresponding `puttytab` is included).
* `puttytel.exe`, `pterm.exe`, and `pageant.exe` in the distribution ZIP are `master` builds, not Direct2D / DirectWrite builds.

## Notes On Building PuTTY CAC Ex

### Prerequisites

* Visual Studio 2022 with the Desktop development with C++ workload (including x64 and ARM64 build tools)
* Upstream PuTTY CAC specifies 2026, and the local environment builds with 2026.
* Windows PowerShell (for building ZIP / Hash files)

### Building

* Execute `packager\build.cmd` to build `x64` and `arm64` release binaries from temporary Git worktrees of `master` and `mod-d2ddw`, generating `putty-cac-ex-0.85.3-exYYYYMMDD-<arch>.zip` and `putty-cac-ex-0.85.3-exYYYYMMDD-hash.txt` under `binaries\` (no code signing or MSI installers are produced)
* Visual Studio solution files for this build are generated under `build\release-bins\` (`master-<arch>` / `d2ddw-<arch>`), and any existing `build\putty.sln` will not be deleted

## Architecture Decision Records (ADR)

### Common (`master` / `mod-d2ddw`)

* **Branch Structure and Distribution Strategy**
  * Maintain a three-branch model: `upstream-master` for tracking upstream (`NoMoreFood/putty-cac`), `master` for the Ranvis integration build, and `mod-d2ddw` for the Direct2D/DirectWrite extension build.
  * For distribution, bundle `putty.exe` from `master` and `putty-d2ddw.exe` from `mod-d2ddw` into a single ZIP archive per architecture.
* **Versioning and Preserving `SSHVER`**
  * Adopt SemVer build metadata notation (`0.85.3+...`) and `BINARY_VERSION 0,85,3,1` for release versions while keeping `SSHVER` as `-Release-0.85-3` (matching upstream PuTTY CAC) for SSH server compatibility.
* **Struct Layout Compatibility with `puttytab`**
  * Because `puttytab` reads the `WinGuiSeat` struct directly via `ReadProcessMemory`, new fields are appended to the end of `WinGuiSeat` without altering offsets of existing members (such as `und_mode`).
* **TSF IME and IMM32 Fallback**
  * Integrate `tsf.cpp` as a loosely coupled `OBJECT` library using a callback struct, enabling TSF inline composition by default with automatic fallback to IMM32 when disabled or if TSF initialization fails.
* **Build Targets, Packaging, and Dependencies**
  * `puttyimp.exe` is temporarily excluded until the `libfido2` LTCG version mismatch is resolved, and will be restored to the build targets once resolved. APIs such as `comctl32.dll` that `putty` does not statically link are resolved dynamically.
  * Release archives are generated via `packager\build.cmd` targeting `x64` and `arm64`, with no signing or MSI packaging, producing `binaries\putty-cac-ex-0.85.3-exYYYYMMDD-<arch>.zip`.
  * `puttytab.exe` in the distribution ZIP is exclusively for `master`'s `putty.exe` and cannot be used with `putty-d2ddw.exe` (no corresponding `puttytab` is bundled). `puttytel.exe`, `pterm.exe`, and `pageant.exe` are GDI builds from `master`, not Direct2D / DirectWrite builds.

### `mod-d2ddw` Specific (Direct2D / DirectWrite Extensions)

* **Removal of GDI Terminal Rendering and Native DirectWrite Migration**
  * Removed GDI dependencies from terminal rendering and the resize size-tip (`sizetip.cpp`), migrating to native font lookup via `IDWriteFactory::GetSystemFontCollection` (with safe fallbacks) and `CF_TTONLY` font dialog filtering.
* **Retained Offscreen Buffer for Differential Redraws and Natural Metrics Alignment**
  * Retain rendered output in an `ID2D1BitmapRenderTarget` (`d2dBRT`) and copy it in a single pass via `ID2D1DeviceContext` (`D2D1_PRIMITIVE_BLEND_COPY`), combining per-changed-cell/run clipping, redundant background clear suppression, and whitespace-only glyph skip to avoid full-screen redraws while preserving transparency, glass effects, and wallpaper compositing.
  * Unify font and glyph width measurement on `GetMetrics` / `GetDesignGlyphMetrics` (with a `+0.5f` rounding tolerance) to match `DWRITE_MEASURING_MODE_NATURAL`, eliminating false-positive horizontal scale transforms and single-glyph run splitting (see [benchmark.md](benchmark.md) for performance benchmark results).
* **Titlebar Font Zoom UI & Font Eyedropper**
  * Structure `fontzoom.cpp` as a loosely coupled `OBJECT` library via `FontZoomCallbacks`, implementing the titlebar top-right zoom overlay, `Ctrl + Wheel` preview, and `WM_COPYDATA` font pickup from other PuTTY windows.
