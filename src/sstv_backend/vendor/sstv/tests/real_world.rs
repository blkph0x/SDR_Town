// Test helpers outside #[test] functions are not covered by the clippy.toml
// test allowances.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! End-to-end test against a real off-air recording captured by a ground
//! station — the true reception path, exercising receiver imperfections (drift,
//! noise, level and DC variation) that synthetic signals do not exhibit.

use std::io::{Cursor, Read};
use std::path::Path;

use sstv::{Decoder, Mode, RgbPixel};

/// A real off-air recording captured by a ground station (32 kHz, mono),
/// stored gzip-compressed to keep the repository small.
const REAL_RECORDING: &str = "tests/assets/real_recording.wav.gz";
/// The source image the recording depicts.
const SOURCE_IMAGE: &str = "examples/patch.png";

fn require(path: &str, generate_with: &str) {
    assert!(
        Path::new(path).exists(),
        "{path} not found. Generate it with `{generate_with}`",
    );
}

/// Load an image as raw row-major RGB bytes.
fn image_bytes(path: &str) -> Vec<u8> {
    image::open(path).expect("open image").to_rgb8().into_raw()
}

/// Flatten pixels to raw row-major RGB bytes.
fn pixels_to_bytes(pixels: &[RgbPixel]) -> Vec<u8> {
    let mut bytes = Vec::with_capacity(pixels.len() * 3);
    for pixel in pixels {
        bytes.extend_from_slice(&[pixel.red(), pixel.green(), pixel.blue()]);
    }
    bytes
}

/// Mean absolute per-channel error between two equal-length RGB byte buffers.
fn mean_abs_error(a: &[u8], b: &[u8]) -> f64 {
    assert_eq!(a.len(), b.len());
    let sum: u64 = a
        .iter()
        .zip(b)
        .map(|(x, y)| u64::from((i32::from(*x) - i32::from(*y)).unsigned_abs()))
        .sum();
    sum as f64 / a.len() as f64
}

/// Read a gzip-compressed WAV, returning its samples (first channel) and rate.
fn read_wav_gz(path: &str) -> (Vec<i16>, u32) {
    let file = std::fs::File::open(path).expect("open wav.gz");
    let mut bytes = Vec::new();
    flate2::read::GzDecoder::new(file)
        .read_to_end(&mut bytes)
        .expect("gunzip wav");
    let reader = hound::WavReader::new(Cursor::new(bytes)).expect("parse wav");
    let channels = reader.spec().channels as usize;
    let sample_rate = reader.spec().sample_rate;
    let samples = reader
        .into_samples::<i16>()
        .map(|sample| sample.expect("read sample"))
        .step_by(channels)
        .collect();
    (samples, sample_rate)
}

/// The decoder should reconstruct a real off-air recording captured by a ground
/// station — the true end-to-end path, with real receiver imperfections that
/// synthetic signals do not exhibit.
#[test]
fn decodes_real_ground_station_recording() {
    require(REAL_RECORDING, "record an off-air Robot36 transmission");

    let (samples, sample_rate) = read_wav_gz(REAL_RECORDING);

    let decoded = Decoder::from_samples(Mode::Robot36, samples.into_iter(), sample_rate)
        .images()
        .next()
        .expect("an image in the recording");
    assert!(decoded.complete(), "image should decode completely");

    let decoded_bytes = pixels_to_bytes(decoded.pixels());
    let reference = image_bytes(SOURCE_IMAGE);
    let error = mean_abs_error(&reference, &decoded_bytes);
    // Real reception drifts a little in colour/timing; a broken decode is 40+.
    assert!(error < 15.0, "decode error {error} too high");
}
