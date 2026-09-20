use alloc::boxed::Box;
use alloc::vec::Vec;

use crate::image::{RgbPixel, YuvPixel};
use crate::modes::layout::{Channel, ColorMode, Layout, Step};
use crate::modes::{Mode, value_frequency};
use crate::synthesizer::Tone;
use crate::{Error, Result};

/// `Encoder` is the main struct for converting an image into SSTV tones.
///
/// Construct `Encoder` with your desired mode and an iterator over the pixels
/// you want to encode, supplied row by row, top to bottom. The image must be
/// [`Mode::image_width`] pixels wide and [`Mode::image_height`] pixels tall.
/// ```rust
/// use sstv::{Encoder, Error, Mode, RgbPixel};
///
/// let image = [RgbPixel::new(0, 0, 0); 320 * 240];
/// let encoder = Encoder::new(Mode::Robot36, image.into_iter()).expect("error during encoding");
/// for tone in encoder {
///     // emit or save the tones
/// }
/// ```
pub struct Encoder {
    inner: Box<dyn Iterator<Item = Tone>>,
}

impl Encoder {
    /// Construct an `Encoder` from the mode and a pixel iterator.
    ///
    /// # Errors
    ///
    /// [`Error::EmptyImage`] if the iterator cannot fill the mode's first
    /// lines.
    pub fn new<I>(mode: Mode, pixels: I) -> Result<Self>
    where
        I: Iterator<Item = RgbPixel> + 'static,
    {
        Ok(Self {
            inner: Box::new(LineEncoder::new(mode, pixels)?),
        })
    }

    /// The transmission as a complete mono 16-bit PCM WAV at the given sample
    /// rate; see [`Synthesizer::to_wav`].
    #[cfg(feature = "wav")]
    #[must_use]
    pub fn to_wav(self, sample_rate: u32) -> Vec<u8> {
        crate::Synthesizer::new(self, sample_rate).to_wav()
    }

    /// The transmission as a complete mono 128 kbps MP3 at the given sample
    /// rate; see [`Synthesizer::to_mp3`].
    ///
    /// # Errors
    ///
    /// Fails if LAME rejects the sample rate.
    #[cfg(feature = "mp3")]
    pub fn to_mp3(
        self,
        sample_rate: u32,
    ) -> core::result::Result<Vec<u8>, mp3lame_encoder::BuildError> {
        crate::Synthesizer::new(self, sample_rate).to_mp3()
    }
}

#[cfg(feature = "image")]
impl Encoder {
    /// Encode an image loaded with the `image` crate.
    ///
    /// The image is resized to the mode's resolution if it does not match,
    /// stretching it to fit.
    ///
    /// ```no_run
    /// use sstv::{Encoder, Mode};
    ///
    /// let image = image::open("image.png").expect("load image");
    /// let encoder = Encoder::from_image(Mode::Robot36, &image).expect("encode image");
    /// ```
    ///
    /// # Errors
    ///
    /// [`Error::EmptyImage`] if the mode has no pixels, which cannot happen
    /// for the supported modes.
    pub fn from_image(mode: Mode, image: &image::DynamicImage) -> Result<Self> {
        let (width, height) = (mode.image_width(), mode.image_height());
        let image = if (image.width(), image.height()) == (width, height) {
            image.to_rgb8()
        } else {
            image
                .resize_exact(width, height, image::imageops::FilterType::Triangle)
                .to_rgb8()
        };
        // `Encoder::new` needs an owned (`'static`) iterator, so the pixels
        // cannot be borrowed from the image buffer.
        #[allow(clippy::needless_collect)]
        let pixels: Vec<RgbPixel> = image
            .pixels()
            .map(|pixel| RgbPixel::new(pixel[0], pixel[1], pixel[2]))
            .collect();
        Self::new(mode, pixels.into_iter())
    }
}

impl Iterator for Encoder {
    type Item = Tone;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }
}

/// Where the encoder is within the transmission.
#[derive(Debug, Clone, Copy, PartialEq)]
enum Phase {
    NotStarted,
    /// Emitting the mode's header tones.
    Header(usize),
    /// Emitting the repeating timing sequences. `line` is the index of the
    /// first image line buffered for the current pass through the sequences.
    Line {
        line: usize,
        sequence: usize,
        step: usize,
        pixel: usize,
    },
    Finished,
}

impl Phase {
    /// Step to the phase that emits the next tone.
    fn advance(&mut self, mode: Mode, layout: &Layout) {
        match *self {
            Self::NotStarted => *self = Self::Header(0),
            Self::Header(index) => {
                *self = if mode.header_tone(index + 1).is_some() {
                    Self::Header(index + 1)
                } else {
                    Self::Line {
                        line: 0,
                        sequence: 0,
                        step: 0,
                        pixel: 0,
                    }
                };
            }
            Self::Line {
                line,
                sequence,
                step,
                pixel,
            } => {
                let steps = layout.sequences[sequence];
                let mid_scan = matches!(steps[step], Step::Scan(..)) && pixel + 1 < layout.width;
                *self = if mid_scan {
                    Self::Line {
                        line,
                        sequence,
                        step,
                        pixel: pixel + 1,
                    }
                } else if step + 1 < steps.len() {
                    Self::Line {
                        line,
                        sequence,
                        step: step + 1,
                        pixel: 0,
                    }
                } else if sequence + 1 < layout.sequences.len() {
                    Self::Line {
                        line,
                        sequence: sequence + 1,
                        step: 0,
                        pixel: 0,
                    }
                } else if line + layout.lines_per_cycle() < layout.height {
                    Self::Line {
                        line: line + layout.lines_per_cycle(),
                        sequence: 0,
                        step: 0,
                        pixel: 0,
                    }
                } else {
                    Self::Finished
                };
            }
            Self::Finished => (),
        }
    }
}

/// Encodes any mode by walking its [`Layout`]: the header, then for each
/// group of buffered lines the mode's timing sequences, emitting fixed tones
/// verbatim and expanding each scan step into one tone per pixel.
struct LineEncoder<I>
where
    I: Iterator<Item = RgbPixel>,
{
    mode: Mode,
    layout: Layout,
    pixels: I,
    /// The image lines carried by the current pass through the sequences —
    /// one line for most modes, the line pair for Robot 36 and PD modes.
    lines: Vec<Vec<RgbPixel>>,
    phase: Phase,
}

impl<I> LineEncoder<I>
where
    I: Iterator<Item = RgbPixel>,
{
    fn new(mode: Mode, mut pixels: I) -> Result<Self> {
        let layout = mode.layout();
        let mut lines = Vec::with_capacity(layout.lines_per_cycle());
        for _ in 0..layout.lines_per_cycle() {
            let mut line = Vec::with_capacity(layout.width);
            if Self::fill_line(&mut pixels, &mut line, layout.width).is_none() {
                return Err(Error::EmptyImage);
            }
            lines.push(line);
        }
        Ok(Self {
            mode,
            layout,
            pixels,
            lines,
            phase: Phase::NotStarted,
        })
    }

    /// Refill `line` in place from the pixel iterator, reusing its allocation.
    fn fill_line(pixels: &mut I, line: &mut Vec<RgbPixel>, width: usize) -> Option<()> {
        line.clear();
        for _ in 0..width {
            line.push(pixels.next()?);
        }
        Some(())
    }

    /// Replace the buffered lines with the next ones from the pixel iterator.
    /// `None` once the image runs out of complete line groups.
    fn buffer_next_lines(&mut self) -> Option<()> {
        for line in &mut self.lines {
            Self::fill_line(&mut self.pixels, line, self.layout.width)?;
        }
        Some(())
    }

    /// The pixel value a scan step transmits at horizontal position `x`.
    fn value(&self, sequence: usize, channel: Channel, x: usize) -> u8 {
        let first_line = sequence * self.layout.lines_per_sequence;
        match channel {
            Channel::Red => self.rgb(first_line, x).red(),
            Channel::Green => self.rgb(first_line, x).green(),
            Channel::Blue => self.rgb(first_line, x).blue(),
            Channel::Y => self.yuv(first_line, x).luma(),
            Channel::YSecond => self.yuv(first_line + 1, x).luma(),
            Channel::RY => self.chroma(first_line, x, YuvPixel::chroma_red),
            Channel::BY => self.chroma(first_line, x, YuvPixel::chroma_blue),
        }
    }

    fn rgb(&self, line: usize, x: usize) -> RgbPixel {
        self.lines[line][x]
    }

    fn yuv(&self, line: usize, x: usize) -> YuvPixel {
        YuvPixel::from(self.rgb(line, x))
    }

    /// One colour-difference component, averaged over all buffered lines
    /// where the mode calls for it (Robot 36 and PD modes).
    fn chroma(&self, line: usize, x: usize, component: fn(YuvPixel) -> u8) -> u8 {
        match self.layout.color {
            ColorMode::YuvAveragedPair | ColorMode::YuvSharedPair => {
                let sum: u16 = (0..self.lines.len())
                    .map(|buffered| u16::from(component(self.yuv(buffered, x))))
                    .sum();
                (sum / self.lines.len() as u16) as u8
            }
            _ => component(self.yuv(line, x)),
        }
    }

    /// Whether the phase just moved onto the first tone of a line cycle whose
    /// lines are not buffered yet. The first cycle's lines are already
    /// buffered at construction.
    const fn needs_next_lines(&self) -> bool {
        matches!(
            self.phase,
            Phase::Line {
                line,
                sequence: 0,
                step: 0,
                pixel: 0,
            } if line > 0
        )
    }

    /// The tone belonging to the current phase.
    fn emit(&self) -> Option<Tone> {
        match self.phase {
            Phase::NotStarted | Phase::Finished => None,
            Phase::Header(index) => self.mode.header_tone(index),
            Phase::Line {
                sequence,
                step,
                pixel,
                ..
            } => match self.layout.sequences[sequence][step] {
                Step::Tone(tone) => Some(tone),
                Step::Scan(channel, duration) => {
                    let value = self.value(sequence, channel, pixel);
                    Some(Tone::new(
                        value_frequency(value),
                        duration / self.layout.width as u32,
                    ))
                }
            },
        }
    }
}

impl<I> Iterator for LineEncoder<I>
where
    I: Iterator<Item = RgbPixel>,
{
    type Item = Tone;

    fn next(&mut self) -> Option<Tone> {
        self.phase.advance(self.mode, &self.layout);

        let pixel_iterator_is_empty = self.needs_next_lines() && self.buffer_next_lines().is_none();
        if pixel_iterator_is_empty {
            self.phase = Phase::Finished;
        }

        self.emit()
    }
}
