# Acknowledgements

SDR Town builds on decades of work by the radio, amateur, aviation, DSP and
open-source communities. We do not claim to have invented their protocols,
algorithms or ideas. Thank you to the people who publish implementations,
research, documentation and test material, and to the testers who help us
understand real radios and real reception conditions.

This is the project's maintained credits register, based on the source tree,
development decisions and documented development tools as of 3 October 2026.
It is not a claim that every named project is bundled, that every proposed
tool was used, or that the list is an exhaustive history of every conversation.
References do not imply affiliation, endorsement or permission to relicense.
For licensing scope and unresolved provenance, read [LICENSING.md](LICENSING.md).

## Components used in the application

| Project / people | Contribution to SDR Town |
| --- | --- |
| [Qt](https://www.qt.io/) and contributors | Desktop interface, widgets, networking and platform integration. |
| [SoapySDR](https://github.com/pothosware/SoapySDR), [SoapyRTLSDR](https://github.com/pothosware/SoapyRTLSDR), [SoapySDRPlay3](https://github.com/pothosware/SoapySDRPlay3) | Hardware abstraction and device modules. |
| [Osmocom RTL-SDR](https://osmocom.org/projects/rtl-sdr/wiki), [libusb](https://github.com/libusb/libusb), [SDRplay](https://www.sdrplay.com/) | Device access, drivers, APIs and hardware documentation. |
| [miniaudio / David Reid](https://github.com/mackron/miniaudio) | Audio device handling and resampling. |
| [mbelib / szechyjs and contributors](https://github.com/szechyjs/mbelib) | Clear digital voice synthesis. |
| [redsea / Oona Raisanen](https://github.com/windytan/redsea), [liquid-dsp / Joseph Gaeddert and contributors](https://github.com/jgaeddert/liquid-dsp) | RDS processing and DSP building blocks. Original spelling/copyrights are retained in upstream files. |
| [JAERO](https://github.com/jontio/JAERO), [JFFT](https://github.com/jontio/JFFT), [libaeroambe](https://github.com/jontio/libaeroambe) / Jonti and contributors | Native Classic Aero modem, FFT and satellite voice components. |
| [libcorrect](https://github.com/quiet/libcorrect) / Brian Armstrong and contributors | Error-correction building blocks. |
| [libacars](https://github.com/szpajder/libacars) / Tomasz Lemiech and contributors | Aviation application parsing, including ADS-C and CPDLC. |
| [GGMorse](https://github.com/ggerganov/ggmorse) / Georgi Gerganov and contributors | Morse detection, timing and decoding. |
| [unexcellent/sstv](https://github.com/unexcellent/sstv) / Tobias Klockau | SSTV image decoder used by the helper. |
| [aholinch/sgp4](https://github.com/aholinch/sgp4), Vallado and collaborators | SGP4 implementation and published orbit-verification work. |
| [spdlog](https://github.com/gabime/spdlog), [fmt](https://github.com/fmtlib/fmt), [nlohmann/json](https://github.com/nlohmann/json) | Logging, formatting and structured data. |
| [libsodium](https://github.com/jedisct1/libsodium), [Jansson](https://github.com/akheron/jansson), [zlib](https://zlib.net/) | Signature verification, application JSON and bounded decompression. |
| [Rust](https://www.rust-lang.org/) / [libm](https://github.com/rust-lang/libm) | SSTV helper language/runtime and math support. |

Optional integrations also acknowledge [ONNX Runtime](https://github.com/microsoft/onnxruntime),
[OpenAI Whisper](https://github.com/openai/whisper) and
[faster-whisper](https://github.com/SYSTRAN/faster-whisper), including the
PyTorch/CTranslate2 and model ecosystems used by those STT backends. Optional
installation does not mean these runtimes or models ship in every release.
Exact notices and component scope are in the [license inventory](LICENSING.md#component-inventory).

## Implementations and workflows studied

These projects helped with understanding, comparison, fixtures or workflow
design. This section is not a blanket assertion that no code was adapted:
the SDRTrunk-derived tuning helper is explicitly identified in LICENSING.md.

- [SDRTrunk / Dennis Sheirer and contributors](https://github.com/DSheirer/sdrtrunk):
  P25 framing, channel tuning, call/slot lifecycle, audio ordering and aliases.
  The tuning-helper provenance requires the separate licensing review above.
- [OP25, including boatbod's fork](https://github.com/boatbod/op25): P25 DSP,
  synchronization, framing, voice-codeword and decoding cross-checks.
- [InmarScope / SarahRoseLives and contributors](https://github.com/SarahRoseLives/InmarScope):
  Aero watch lists, voice/data workflows, aircraft identity/map presentation and
  SDRplay packaging comparisons. Native receive reuse is separately credited to
  JAERO/libaeroambe/libacars; InmarScope credit is not an MIT license for its code.
- [QSSTV](https://github.com/ON4QZ/QSSTV) and
  [colaclanth/sstv](https://github.com/colaclanth/sstv): SSTV mode/VIS references
  and independent decode/image comparisons; distinct from the shipped helper.
- [GNU Radio](https://github.com/gnuradio/gnuradio),
  [SvxLink](https://github.com/sm0svx/svxlink) and
  [SpanDSP](https://github.com/freeswitch/spandsp): tone detection, selectivity,
  streaming DSP and DTMF algorithm references. The DTMF pass documented in
  [DEC-0168](docs/DECISIONS.md) did not import SpanDSP implementation code.
- [Hamlib](https://github.com/Hamlib/Hamlib): rotor/controller protocol and
  capability references. Speaking a compatible protocol is distinct from
  bundling the library or promising physical-controller qualification.
- [SatDump](https://github.com/SatDump/SatDump) and
  [gr-satellites](https://github.com/daniestevez/gr-satellites): public satellite
  decoder architecture and roadmap research, not a claim of shipped decoder parity.
- [acarsdec](https://github.com/TLeconte/acarsdec),
  [dumpvdl2](https://github.com/szpajder/dumpvdl2) and
  [vdlm2dec](https://github.com/TLeconte/vdlm2dec): aviation-decoder research and
  roadmap references, not an assertion these backends are integrated.
- [WebSDR](https://www.websdr.org/), [KiwiSDR](https://kiwisdr.com/) and
  [GQRX](https://gqrx.dk/): inspiration for accessible local/networked radio use.
  SDR Town and the [FUBAR companion](https://github.com/blkph0x/FUBAR) are
  independent projects, not official extensions of these applications.

The detailed protocol/source evidence is maintained in
[SPEC_INDEX.md](docs/SPEC_INDEX.md), [DECISIONS.md](docs/DECISIONS.md) and the
component provenance files; this credits page does not replace them.

## Standards, data and community knowledge

- [ITU](https://www.itu.int/): published radio/telecommunication recommendations,
  including [M.1677 Morse](https://www.itu.int/rec/R-REC-M.1677-1-200910-I) and
  [Q.24 DTMF](https://www.itu.int/rec/T-REC-Q.24-198811-I). Standards references
  are not redistributed standards texts or claims of certification.
- [TIA](https://tiaonline.org/) and the P25 engineering community: protocol
  specifications and interoperability knowledge. Availability and provenance
  limits are explicitly recorded in the specification index.
- [ICAO](https://www.icao.int/) and the aviation datalink community: addressing
  and aviation protocol documentation;
  [ibosoftnet/icao-aircraft-addresses](https://github.com/ibosoftnet/icao-aircraft-addresses)
  for the separately licensed address-allocation transcription.
- [CelesTrak](https://celestrak.org/), Vallado and collaborators: orbit data,
  SGP4 references and verification vectors.
- [Natural Earth](https://www.naturalearthdata.com/),
  [OpenStreetMap contributors](https://www.openstreetmap.org/copyright),
  [OpenSky Network](https://opensky-network.org/) and
  [ADSB.lol](https://www.adsb.lol/): basemap or optional online aircraft sources.
  Each provider's attribution, data and service terms apply independently.
- Wilson/Sergi.vdl2 and [thebaldgeek](https://thebaldgeek.github.io/): published
  L-band frequency survey. Exact provenance and historical limits are in
  [the band-plan notes](data/inmarsat/README.md).
- [RadioReference](https://www.radioreference.com/) and community alias-list
  contributors, including [fubarzi/TG-SITES](https://github.com/fubarzi/TG-SITES):
  operator-supplied channel/identity knowledge and import interoperability.
  Import support does not grant redistribution rights to someone else's database.
- Researchers, radio amateurs and field testers who provide independent
  recordings, device reports and DSP explanations. Private captures, identities
  and transcripts are not published as part of these credits without consent.

## Development and verification tools

- [OpenAI ChatGPT and Codex](https://openai.com/), [xAI Grok](https://x.ai/) and
  [Cursor](https://cursor.com/): AI-assisted development and research, as recorded
  by the maintainer. Their outputs still require human review, provenance checks
  and executable tests; use of an AI tool does not establish correctness or rights.
- [Git](https://git-scm.com/), [GitHub](https://github.com/), GitHub Actions and
  GitHub CLI: version control, review, automation and release delivery.
- [CMake/CTest/CPack](https://cmake.org/),
  [Microsoft Visual Studio/MSVC](https://visualstudio.microsoft.com/),
  [vcpkg](https://github.com/microsoft/vcpkg),
  [MSYS2/MinGW-w64](https://www.msys2.org/), GCC, Rust/Cargo and
  [NSIS](https://nsis.sourceforge.io/): build, dependency and packaging toolchains.
- [Python](https://www.python.org/), NumPy/SciPy, PowerShell,
  [Catch2](https://github.com/catchorg/Catch2), Qt Test and
  [Playwright](https://playwright.dev/): fixtures, numerical analysis,
  regression tests and desktop/web workflow qualification.
- [SoundFile/libsndfile](https://python-soundfile.readthedocs.io/),
  [Pillow](https://python-pillow.org/) and [PyAV/FFmpeg](https://pyav.org/):
  development-only audio/image fixtures and reference conversion. They have
  their own dependency/build-specific licenses, not this project's MIT grant.
- [Windows Debugging Tools / WinDbg and CDB](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/):
  native lifecycle and crash investigation; repeatable CDB checks are documented
  in [NATIVE_RUNTIME_QA.md](docs/NATIVE_RUNTIME_QA.md).

## Keeping credits accurate

When adding or adapting a dependency, algorithm reference, data source or tool,
update this register and its precise source/provenance notice in the same change.
Record the version/commit, actual use, local modifications and applicable terms.
Keep original authors' notices intact. Do not turn a roadmap mention into a
claim of integration, and do not treat this page as a substitute for required
license texts or corresponding source. Please report missing or incorrect
credits through the project's GitHub issues so they can be corrected.
