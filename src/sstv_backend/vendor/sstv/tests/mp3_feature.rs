// Test helpers outside #[test] functions are not covered by the clippy.toml
// test allowances.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! Tests for the `mp3` feature: encoding to and decoding from in-memory MP3s.

use sstv::{Decoder, Encoder, Mode, RgbPixel};

#[test]
fn encodes_a_transmission_into_an_mp3() {
    let image = vec![RgbPixel::new(128, 64, 32); 320 * 240];
    let encoder = Encoder::new(Mode::Robot36, image.into_iter()).expect("construct encoder");

    let mp3 = encoder.to_mp3(24_000).expect("encode mp3");

    // A 36 second transmission at 128 kbps is roughly half a megabyte; a
    // broken encoding would be empty or tiny. Frames start on a sync word.
    assert!(mp3.len() > 100_000, "mp3 is only {} bytes", mp3.len());
    assert_eq!(mp3[0], 0xFF, "mp3 should start with a frame sync word");
}

#[test]
fn round_trips_through_an_mp3() {
    let (width, height) = (Mode::Robot36.image_width(), Mode::Robot36.image_height());
    let mut image = Vec::with_capacity((width * height) as usize);
    for y in 0..height {
        for x in 0..width {
            let red = (x * 255 / (width - 1)) as u8;
            let green = (y * 255 / (height - 1)) as u8;
            let blue = ((x + y) * 255 / (width + height - 2)) as u8;
            image.push(RgbPixel::new(red, green, blue));
        }
    }
    let encoder = Encoder::new(Mode::Robot36, image.clone().into_iter()).expect("encode");
    let mp3 = encoder.to_mp3(24_000).expect("encode mp3");

    let decoded = Decoder::from_mp3(Mode::Auto, &mp3)
        .expect("parse mp3")
        .images()
        .next()
        .expect("an image");

    assert_eq!(decoded.mode(), Mode::Robot36);
    assert!(decoded.complete(), "image should decode completely");
    let total: u64 = image
        .iter()
        .zip(decoded.pixels())
        .map(|(p, q)| {
            let d = |x: u8, y: u8| u64::from((i32::from(x) - i32::from(y)).unsigned_abs());
            d(p.red(), q.red()) + d(p.green(), q.green()) + d(p.blue(), q.blue())
        })
        .sum();
    let error = total as f64 / (image.len() as f64 * 3.0);
    // Lossy compression costs some accuracy on top of the usual round trip.
    assert!(error < 16.0, "mean abs error {error} too high");
}
