// Examples fail fast on bad input by design.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! Encode examples/patch.png into an SSTV WAV using this crate's encoder.
//! Loading the image is done with the `image` crate.
//!
//! ```text
//! cargo run --features image,wav --example encode -- local/encoded.wav [mode] [sample_rate]
//! ```

use sstv::{Encoder, Mode};
use std::env;

fn parse_mode(name: &str) -> Mode {
    Mode::ALL
        .into_iter()
        .chain([Mode::Auto])
        .find(|mode| format!("{mode:?}").eq_ignore_ascii_case(name))
        .unwrap_or_else(|| panic!("unknown mode {name}, expected one of {:?}", Mode::ALL))
}

fn main() {
    let mut args = env::args().skip(1);
    let output = args
        .next()
        .expect("usage: encode <output.wav> [mode] [sample_rate]");
    let mode = args.next().map_or(Mode::Robot36, |name| parse_mode(&name));
    let sample_rate: u32 = args.next().map_or(48_000, |s| {
        s.parse().expect("sample rate must be an integer")
    });

    let image = image::open("examples/patch.png").expect("open examples/patch.png");
    let encoder = Encoder::from_image(mode, &image).expect("encode");

    std::fs::write(&output, encoder.to_wav(sample_rate)).expect("write wav");

    println!("wrote {output} as {mode:?} at {sample_rate} Hz");
}
