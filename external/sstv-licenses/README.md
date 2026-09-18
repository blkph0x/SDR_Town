# Offline SSTV helper dependencies

The optional `sdrtown_sstv.exe` links the MIT-licensed `unexcellent/sstv`
revision `16bf34aac81b0041f5fdce52a1aef64eea0d5f6e` and `libm` 0.2.16.
Cargo.toml and Cargo.lock pin the build inputs. Original notices are retained
here and packaged under `licenses/sstv/` in binary distributions.
Rust 1.88.0 standard-library third-party notices are in
`rust-COPYRIGHT-library.html`. The helper is a separate process, not a DLL.
Reference recordings/images used for development are not distributed.
