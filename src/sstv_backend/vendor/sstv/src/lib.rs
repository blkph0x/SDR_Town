#![doc = "Slow-Scan Television Encoding With Minimal Memory Usage"]
#![cfg_attr(not(any(feature = "std", test)), no_std)]
#![warn(missing_docs)]

extern crate alloc;

mod error;

mod decoder;
mod demodulator;
mod encoder;
mod image;
mod modes;
mod synthesizer;
mod units;

pub use decoder::{DecodedImage, Decoder, Event, Events, Images, RgbRow};
pub use demodulator::Demodulator;
pub use encoder::Encoder;
pub use error::{Error, Result};
pub use image::{RgbPixel, YuvPixel};
pub use modes::Mode;
pub use synthesizer::{Synthesizer, Tone};
pub use units::{Duration, Frequency};
