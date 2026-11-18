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

## Notes On Building PuTTY CAC Ex

### Prerequisites

* Visual Studio 2022 with the Desktop development with C++ workload
* Upstream PuTTY CAC specifies 2026, and the local environment builds with 2026.
* WiX Toolset (for building MSI files) *Untested
* Windows PowerShell (for building MSI / ZIP / Hash files)

### Building

* Execute `packager\build.cmd` to generate build files
* Visual Studio solution files will be generated under `build`
