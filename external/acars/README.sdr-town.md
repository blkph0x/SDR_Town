# ACARS application decoders

Protocol modules from szpajder/libacars v2.2.1, commit
9af09a0121d4ec577339cbd4c7420d7519da48fa (MIT, see LICENSE.md).
Source: https://github.com/szpajder/libacars/tree/v2.2.1

ADS-C, FANS CPDLC, MIAM, media advisory, OHMA, formatting, CRC and bounded
decompression. ASN.1 generated files/runtime retain their BSD notices; see
UPSTREAM-README.md. Upstream C files are unmodified. SDR Town supplies scoped
CMake/config, a small C CPDLC status adapter (keeps legacy ASN.1 typedefs out of
C++), and bounded C++ dispatch with direction/encoding/error checks.

Jansson (MIT) handles OHMA JSON; zlib handles compression. Their runtime and
licences are packaged. XML pretty-printing is disabled; plain XML is retained.
Application multipart reassembly is not yet enabled. Segments are labelled
unsupported, never complete messages. Proprietary/unknown applications retain
raw text with an uninterpreted status. Do not interpret unsupported encodings
as RF corruption. Raw text remains separate from rendered application output.
