//! Turns the sampled scans of one timing sequence into rows of pixels,
//! according to the mode's colour system.

use alloc::vec::Vec;

use crate::image::{RgbPixel, YuvPixel};
use crate::modes::layout::{Channel, ColorMode, Layout};

/// The sampled contents of one pass through a timing sequence.
pub(super) struct SequenceData {
    /// Each scan step's channel and its sampled pixel values.
    pub scans: Vec<(Channel, Vec<u8>)>,
    /// The frequency sampled at the centre of each non-sync tone step, in the
    /// order the steps occur. Robot 36 reads its separator pulse from here.
    pub tone_hz: Vec<u32>,
}

/// One half of a Robot 36 line pair: its luminance, its colour-difference
/// scan, and the sampled separator-pulse frequency identifying which colour
/// difference it carries.
struct PendingLine {
    luma: Vec<u8>,
    chroma: Vec<u8>,
    separator_hz: u32,
}

/// Combines each sequence's scans into image rows.
pub(super) struct Assembler {
    color: ColorMode,
    /// Index of the tone identifying Robot 36's line parity.
    parity_tone: Option<usize>,
    /// Robot 36: the decoded first line of a pair, awaiting its partner.
    pending_pair: Option<PendingLine>,
}

impl Assembler {
    pub fn new(layout: &Layout) -> Self {
        Self {
            color: layout.color,
            parity_tone: layout.parity_tone(),
            pending_pair: None,
        }
    }

    /// The rows completed by one sequence pass, in transmission order.
    // expect: the layouts guarantee every scan and tone their colour mode
    // relies on.
    #[allow(clippy::expect_used)]
    pub fn assemble(&mut self, data: &SequenceData) -> Vec<Vec<RgbPixel>> {
        let scan = |channel: Channel| {
            data.scans
                .iter()
                .find(|(c, _)| *c == channel)
                .map(|(_, values)| values.as_slice())
                .expect("the mode's timing sequence contains this scan")
        };

        let mut rows: Vec<Vec<RgbPixel>> = Vec::new();
        match self.color {
            ColorMode::Luma => {
                let y = data
                    .scans
                    .first()
                    .map(|(_, values)| values.as_slice())
                    .expect("luma mode has one scan");
                rows.push(y.iter().map(|&v| RgbPixel::new(v, v, v)).collect());
            }
            ColorMode::Rgb => {
                let (red, green, blue) = (
                    scan(Channel::Red),
                    scan(Channel::Green),
                    scan(Channel::Blue),
                );
                rows.push(
                    (0..red.len())
                        .map(|x| RgbPixel::new(red[x], green[x], blue[x]))
                        .collect(),
                );
            }
            ColorMode::Yuv => {
                rows.push(yuv_row(
                    scan(Channel::Y),
                    scan(Channel::RY),
                    scan(Channel::BY),
                ));
            }
            ColorMode::YuvSharedPair => {
                let (ry, by) = (scan(Channel::RY), scan(Channel::BY));
                rows.push(yuv_row(scan(Channel::Y), ry, by));
                rows.push(yuv_row(scan(Channel::YSecond), ry, by));
            }
            ColorMode::YuvAveragedPair => {
                // One line carries only one colour difference; hold it until
                // its partner arrives, then reconstruct both lines from the
                // shared differences. Which line carries which is read from
                // the separator pulse (1500hz on even, 2300hz on odd) rather
                // than assumed from read order — so a decode that began on an
                // odd line does not swap red and blue.
                let separator = self
                    .parity_tone
                    .expect("paired sequences differ in their separator pulse");
                let luma = scan(Channel::Y).to_vec();
                let (_, chroma) = data
                    .scans
                    .iter()
                    .find(|(c, _)| matches!(c, Channel::RY | Channel::BY))
                    .expect("each Robot 36 line carries one colour difference");
                let line = PendingLine {
                    luma,
                    chroma: chroma.clone(),
                    separator_hz: data.tone_hz.get(separator).copied().unwrap_or(0),
                };

                match self.pending_pair.take() {
                    None => self.pending_pair = Some(line),
                    Some(first) => {
                        let first_is_even = first.separator_hz <= line.separator_hz;
                        let (ry, by) = if first_is_even {
                            (&first.chroma, &line.chroma)
                        } else {
                            (&line.chroma, &first.chroma)
                        };
                        rows.push(yuv_row(&first.luma, ry, by));
                        rows.push(yuv_row(&line.luma, ry, by));
                    }
                }
            }
        }
        rows
    }
}

fn yuv_row(luma: &[u8], chroma_red: &[u8], chroma_blue: &[u8]) -> Vec<RgbPixel> {
    luma.iter()
        .zip(chroma_red.iter().zip(chroma_blue.iter()))
        .map(|(&y, (&ry, &by))| RgbPixel::from(YuvPixel::new(y, ry, by)))
        .collect()
}
