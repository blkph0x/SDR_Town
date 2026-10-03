# Licensing and third-party scope

SDR Town's original contributions are available under the [MIT License](LICENSE.txt).
This grant does not relicense third-party code, data, documentation, models,
drivers or other material. Existing upstream notices and terms continue to
apply, including to adapted code outside `external/`. The combined application
is **not represented as an MIT-only distribution**.

The maintainer welcomes reuse, learning and improvements, including commercial
reuse as permitted by the applicable licenses. A personal shout-out is optional;
retaining required copyright and permission notices is not. The project intends
to remain free and open source. The standard MIT text is not supplemented with
a receive-only, noncommercial or attribution-waiver clause.

## Scope and references

- `external/`, the SSTV vendor tree, reference submodules and upstream-derived
  portions keep their own licenses and original copyright holders. A directory
  name is not the test of ownership: per-file notices and provenance matter.
- SDRTrunk and OP25 are credited as technical references. The maintainer
  clarifies that the P25 implementation uses reference principles rather than
  copied code. Implementing the same mathematical ideas or methods does not,
  by itself, import a reference project's software license. See
  [WIPO's idea/expression distinction](https://www.wipo.int/en/web/copyright/protection).
- An earlier version of this document called `include/P25SdrtrunkTune.h` a
  confirmed GPL-derived exception. That conclusion is withdrawn: its historical
  "copied" comment and similar algorithm/control flow warranted investigation
  but did not establish copying of protected expression. The maintainer's
  clarification is recorded in
  [ISS-0059](docs/ISSUES.md#iss-0059---p25-reference-provenance). This correction
  is not an independent certification of the entire source history. Actual
  source adaptation, if established, must still be handled under its terms.
- Third-party rights cannot be waived by crediting the project, translating
  its code, linking to its repository, or adding this MIT license.
- User captures, imported alias lists and downloaded provider data are not
  automatically licensed under SDR Town's MIT grant. Do not redistribute them
  without the necessary rights and consent.
- Optional applications/services and separately installed vendor drivers have
  their own terms. SDRplay's vendor API/service is not relicensed or included
  by permission from this project.

## Component inventory

This is an evidence-based source inventory, not a complete transitive SBOM or
a certification of a particular binary. Exact upstream revisions are recorded
by Git submodule objects, adjacent provenance files, Cargo.lock and build
inputs. Preserve per-file notices even where the table gives a short label.

| Component | Use / applicable upstream terms | Source or notice |
| --- | --- | --- |
| Qt 6 Core, Gui, Widgets, Network | Desktop UI/networking; open-source LGPL/GPL options and third-party notices, or a separate commercial license | [Qt terms](https://www.qt.io/development/open-source-lgpl-obligations) |
| SoapySDR | Device interface; Boost Software License 1.0 | [Upstream](https://github.com/pothosware/SoapySDR) |
| SoapyRTLSDR / SoapySDRPlay3 | Driver modules; MIT, separate from the device runtime licenses | [RTL module](https://github.com/pothosware/SoapyRTLSDR), [SDRplay module](https://github.com/pothosware/SoapySDRPlay3) |
| librtlsdr / libusb | RTL hardware access; GPL / LGPL respectively, with version-specific source/distribution obligations | [RTL-SDR](https://osmocom.org/projects/rtl-sdr/wiki), [libusb](https://github.com/libusb/libusb) |
| SDRplay API | Separately installed vendor runtime; vendor terms, not MIT | [SDRplay](https://www.sdrplay.com/api/) |
| miniaudio | Audio devices/resampling; upstream choice of Unlicense or MIT-0 | [Notice](external/miniaudio/LICENSE) |
| mbelib | P25 voice synthesis; ISC and separate upstream patent warning | [Copyright](external/mbelib/COPYRIGHT), [README](external/mbelib/README.md) |
| redsea subset | RDS block/subcarrier processing; upstream license and retained per-file notices | [License](external/redsea-block/LICENSE), [provenance](external/redsea-block/UPSTREAM.md) |
| liquid-dsp | RDS DSP backend; MIT | [License](external/liquid-dsp/LICENSE) |
| JAERO / JFFT | Native Aero receive/DSP subset and FFT; MIT | [JAERO](external/aero/jaero/LICENSE), [JFFT](external/aero/jfft/LICENSE), [provenance](external/aero/README.sdr-town.md) |
| libcorrect | Aero error correction; BSD-3-Clause | [License](external/aero/correct/LICENSE) |
| libaeroambe | Aero voice codec; ISC codec and MIT wrapper; upstream patent warning retained | [Codec](external/aero/codec/COPYRIGHT), [wrapper](external/aero/codec/LICENSE-wrapper), [README](external/aero/codec/README.md) |
| libacars / ASN.1 runtime | ACARS applications; MIT plus retained BSD notices in generated/runtime sources | [License](external/acars/LICENSE.md), [provenance](external/acars/README.sdr-town.md) |
| Jansson / zlib | Application JSON/decompression; MIT / zlib | [Jansson](external/acars/JANSSON-LICENSE.txt), [zlib](external/acars/ZLIB-LICENSE.txt) |
| GGMorse | Morse detection/decoding; MIT, with JFFT used for local FFT adaptation | [License](external/ggmorse/LICENSE), [provenance](external/ggmorse/README.sdr-town.md) |
| unexcellent/sstv / libm / Rust standard library | Optional SSTV helper; MIT SSTV plus the dependency/runtime terms collected alongside it | [Collected notices](external/sstv-licenses/README.md), [vendor license](src/sstv_backend/vendor/sstv/LICENSE) |
| aholinch/sgp4 | Satellite orbit propagation; Unlicense | [License](external/sgp4/LICENSE), [provenance](external/sgp4/README.sdr-town.md) |
| spdlog / fmt | Logging/formatting; MIT with retained upstream notices | [spdlog](https://github.com/gabime/spdlog), [fmt](https://github.com/fmtlib/fmt) |
| nlohmann/json | JSON; MIT | [Upstream](https://github.com/nlohmann/json) |
| libsodium | Update signature verification; ISC and retained third-party notices | [Upstream](https://github.com/jedisct1/libsodium) |
| Catch2 | Test-only framework; Boost Software License 1.0 | [Upstream](https://github.com/catchorg/Catch2) |
| ONNX Runtime | Optional build integration, not a claim that a trained classifier ships; MIT and dependency terms | [Upstream](https://github.com/microsoft/onnxruntime) |
| Whisper / faster-whisper | Optional external STT backends; their licenses and model/dependency terms apply independently | [Whisper](https://github.com/openai/whisper), [faster-whisper](https://github.com/SYSTRAN/faster-whisper) |
| Natural Earth | Offline overview basemap; public domain | [Pinned source/terms](resources/maps/README.md) |
| ICAO allocation transcription | Address-country allocation reference; CC0 for the pinned transcription, not blanket rights to ICAO publications | [Source/terms](docs/INMARSAT_MONITOR.md) |

Build/runtime dependencies can additionally include PThreads4W, compiler
support libraries, Microsoft Visual C++ runtime, Qt plugins and their codecs.
Their notices and redistribution terms must be inventoried from each actual
package. `vcpkg.json` is a direct dependency manifest, not a complete license
report. Compiler/runtime exceptions and dynamic linking must not be assumed
to make every dependency MIT.

## Binary release gate

The public 0.2.122 portable ZIP was inspected for this change. It contains many
upstream notices but does **not** contain a complete license inventory or the
root project license. This document does not retrospectively certify that ZIP.
See [ISS-0060](docs/ISSUES.md#iss-0060---binary-licensesource-notice-inventory-incomplete-2026-10-03-open).

T-0104 now stages project, codec and configured vcpkg notices and generates an
exact-file/hash inventory. This does not complete transitive/source/relink
coverage. Publication is mechanically blocked while that work remains; source
CI uploads inventory evidence rather than another incomplete binary package.
See the [package ledger](docs/PACKAGE_HARDENING_20261003.md).

Before publishing another binary:

1. Distinguish original implementations of reference ideas from actual source
   reuse. Record the latter's provenance and applicable terms; do not infer
   derivation or license obligations from mathematical similarity alone.
2. Inventory the exact executable, libraries, plugins, helpers, runtime files,
   models and data. Preserve upstream notices and identify exact source versions.
3. Ship this project's license/scope/credits and all required third-party texts.
   Meet corresponding-source, relinking and installation-information obligations
   where applicable; an upstream homepage alone is not a compliance bundle.
4. Check the **downloaded** archive and installer, not just the source tree.
   Add package tests for required notices and retain the evidence with the release.

This documentation change does not alter radio/DSP behavior, bump the app
version, replace existing assets or certify patent clearance. A qualified
license/provenance review is required for unresolved combined-work questions.

## Operational guidance

The project's supported decoding policy is lawful reception of unencrypted
signals. Users are responsible for reception, recording, transmission and
data-use rules where they operate. This guidance describes project policy and
user responsibilities; it is not an extra restriction added to MIT.

Credits and the distinction between integration and learning references:
[ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).
