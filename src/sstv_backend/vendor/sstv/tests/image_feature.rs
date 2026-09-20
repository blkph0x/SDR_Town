// Test helpers outside #[test] functions are not covered by the clippy.toml
// test allowances.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! Tests for the `image` feature: converting decoded images into `image`
//! crate buffers.

use sstv::{Decoder, Encoder, Mode, RgbPixel, Synthesizer};

const SAMPLE_RATE: u32 = 24_000;

/// A full transmission of a test image with variation in all three channels.
fn transmission(mode: Mode) -> Vec<i16> {
    let (width, height) = (mode.image_width(), mode.image_height());
    let mut pixels = Vec::with_capacity((width * height) as usize);
    for y in 0..height {
        for x in 0..width {
            let red = (x * 255 / (width - 1)) as u8;
            let green = (y * 255 / (height - 1)) as u8;
            let blue = ((x + y) * 255 / (width + height - 2)) as u8;
            pixels.push(RgbPixel::new(red, green, blue));
        }
    }
    let encoder = Encoder::new(mode, pixels.into_iter()).expect("construct encoder");
    Synthesizer::new(encoder, SAMPLE_RATE).collect()
}

#[test]
fn encodes_image_buffers_resizing_them_to_the_mode_resolution() {
    let small = image::DynamicImage::ImageRgb8(image::RgbImage::from_fn(100, 80, |x, y| {
        image::Rgb([(x * 2) as u8, (y * 3) as u8, 128])
    }));
    let encoder = Encoder::from_image(Mode::Robot36, &small).expect("encode image");

    let decoded = Decoder::from_samples(
        Mode::Robot36,
        Synthesizer::new(encoder, SAMPLE_RATE),
        SAMPLE_RATE,
    )
    .images()
    .next()
    .expect("an image");
    assert!(decoded.complete(), "image should decode completely");
    assert_eq!(decoded.width() as u32, Mode::Robot36.image_width());
    assert_eq!(decoded.height() as u32, Mode::Robot36.image_height());
}

#[test]
fn decoded_images_convert_to_image_buffers() {
    let samples = transmission(Mode::Robot36);

    let decoded = Decoder::from_samples(Mode::Robot36, samples.into_iter(), SAMPLE_RATE)
        .images()
        .next()
        .expect("an image");
    let buffer = image::RgbImage::from(&decoded);

    assert_eq!(buffer.width() as usize, decoded.width());
    assert_eq!(buffer.height() as usize, decoded.height());
    let pixel = decoded.pixels()[decoded.width() + 1];
    assert_eq!(
        buffer.get_pixel(1, 1),
        &image::Rgb([pixel.red(), pixel.green(), pixel.blue()])
    );
}

#[test]
fn decodes_to_image_buffers_and_saves_them() {
    let samples = transmission(Mode::Robot36);

    let images: Vec<image::RgbImage> =
        Decoder::from_samples(Mode::Robot36, samples.into_iter(), SAMPLE_RATE)
            .rgb_images()
            .collect();
    assert_eq!(images.len(), 1, "expected exactly one image");
    assert_eq!(images[0].width(), Mode::Robot36.image_width());
    assert_eq!(images[0].height(), Mode::Robot36.image_height());

    let path = std::env::temp_dir().join("sstv-image-feature-test.png");
    images[0].save(&path).expect("save decoded image");
    let reloaded = image::open(&path).expect("reload decoded image").to_rgb8();
    assert_eq!(&reloaded, &images[0]);
    std::fs::remove_file(&path).expect("remove test output");
}
