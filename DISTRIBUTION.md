# SDR Town binary distribution and rebuilding

The RTL-enabled combined application is distributed under **GNU GPL version 3
or later**, with no warranty. See `licenses/distribution/GPL-3.0.txt` in the
portable package, or `resources/licenses/GPL-3.0.txt` in the source tree.
This selects the later-version option in librtlsdr's GPL-2.0-or-later grant.
The original SDR Town source remains available under its existing MIT grant;
third-party copyrights, permissive grants and additional permissions remain.
This is not an MIT-only binary. Independently distributed tools, vendor drivers
and system runtimes retain their own terms, not a new GPL label from this project.

## Sources and notices included with every portable release

- `licenses/project/source-materials.zip`: exact application, modifications,
  embedded libraries, data, CMake scripts and recorded submodule objects.
- `licenses/vcpkg/source-materials.zip`: exact dependency sources, patches,
  recipes, copyright texts and SPDX build receipts.
- `licenses/vcpkg/tooling-materials.zip`: matching build scripts, triplets and
  helper ports. Bootstrap/tool downloads require a network connection.
- `licenses/qt/source-materials.zip`: exact Qt sources, configuration headers,
  complete attribution catalogs and rebuild/replacement instructions.
- `licenses/distribution/materials.zip`: component review, extra source
  archives, driver-module sources and identified RDS compiler-runtime evidence.
- `licenses/`: original upstream notices, codec patent warnings, Qt/USB
  replacement-test reports and Microsoft runtime licence/installer evidence.

Qt Core, Gui, Widgets, Network and Svg are used dynamically under LGPLv3,
with their original third-party terms. Users may modify, reverse engineer for
debugging modifications, relink and run replacement libraries. SDR Town does
not require a vendor signature to run user replacements. Update verification
does not restrict running your own local build. No device keys are needed.

Where included by Qt, this software is based in part on the work of the
Independent JPEG Group and uses the FreeType Project (www.freetype.org).
FreeType's FTL option is selected, not its GPL-2.0-only alternative. Qt's
optional source components are catalogued without claiming every one is linked.
Windows-only binaries do not include QtTest, Wayland, Android or macOS plugins.

The RDS DLL uses GCC's GPLv3 runtime with the GCC Runtime Library Exception
3.1, and MinGW-w64 runtime terms. Its separate GCC compilation uses source
inputs and no proprietary GCC intermediate-code optimizer. Original runtime
notices and versions are included. The toolchain itself is not shipped as part
of the application. MSVC and Windows runtimes retain Microsoft's terms.

## Rebuild and replace

Unpack project sources into an empty directory, and extract the dependency
recipes/tooling alongside it. The source CMake/workflow files give the exact
options. Install the required x64 Visual Studio 2022 compiler/Windows SDK,
CMake, Python, Git, Qt SDK of the recorded version and Rust 1.88.0. These
general-purpose development tools are prerequisites, not application binaries.
The RDS compiler is downloaded and hash-verified by `scripts/rds_toolchain.py`.
Do not use a random compiler found on PATH for an official package.

For dependency replacement, run `scripts/test_usb_replacement.py` against the
portable stage. It reconstructs vcpkg from the shipped scripts/recipes,
disables binary caches, builds rtlsdr/libusb/pthreads and verifies replacement
loading in a disposable copy. Exact source downloads are checksum-verified.
For Qt replacement use `scripts/test_qt_replacement.py --full` and the Qt kit's
instructions. Close SDR Town before replacing DLLs, keep a backup and use
matching x64 Release DLLs and plugins. RF hardware is not needed for loader QA.

SDRplay's proprietary API/service is separately installed from its vendor and
is not included in this archive. Its independent MIT Soapy module source is
included. External STT models, network-provider data and user IQ/audio are not
redistributed with the application. No patent clearance is asserted; retained
upstream warnings and applicable local reception rules still matter.

The packaging checks establish specified source, notice and replacement-test
coverage. They are not a legal opinion, proof of every historic contribution's
ownership, or a promise of RF performance.
