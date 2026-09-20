//! The SSTV modes specified in the Dayton paper — JL Barber (N7CXI),
//! "Proposal for SSTV Mode Specifications", presented at the Dayton SSTV
//! forum, 20 May 2000.
//!
//! Each mode family lives in its own module mirroring a chapter of the paper,
//! transcribing its timing table into a [`Layout`]. Everything
//! shared between modes — the frequency range, the calibration header and the
//! VIS code — is defined here, as in the paper's common sections.

pub mod layout;

mod extra;
mod martin;
mod pasokon;
mod pd;
mod robot;
mod scottie;
mod wrasse;

use crate::synthesizer::Tone;
use crate::units::{Duration, Frequency};
use crate::{Hz, ms};
use layout::Layout;

/// The sync pulse frequency, shared by every mode.
pub const SYNC_FREQUENCY: Frequency = Hz!(1200);
/// Pure black — the lower end of the luminance range.
pub const BLACK_FREQUENCY: Frequency = Hz!(1500);
/// Pure white — the upper end of the luminance range.
pub const WHITE_FREQUENCY: Frequency = Hz!(2300);
/// The leader tone of the calibration header.
pub const LEADER_FREQUENCY: Frequency = Hz!(1900);
const VIS_ONE_FREQUENCY: Frequency = Hz!(1100);
const VIS_ZERO_FREQUENCY: Frequency = Hz!(1300);
/// Every VIS bit (start, data, parity, stop) lasts 30ms.
const VIS_BIT_DURATION: Duration = ms!(30);

/// The frequency representing a pixel value, mapped linearly onto the
/// luminance range.
pub fn value_frequency(value: u8) -> Frequency {
    BLACK_FREQUENCY + (WHITE_FREQUENCY - BLACK_FREQUENCY) * u32::from(value) / 255
}

/// Tuning (VOX) tones customarily sent ahead of the calibration header to
/// open receiver squelch. They are not part of the paper's specification.
const VOX_TONES: [Tone; 8] = [
    Tone::new(Hz!(1900), ms!(100)),
    Tone::new(Hz!(1500), ms!(100)),
    Tone::new(Hz!(1900), ms!(100)),
    Tone::new(Hz!(1500), ms!(100)),
    Tone::new(Hz!(2300), ms!(100)),
    Tone::new(Hz!(1500), ms!(100)),
    Tone::new(Hz!(2300), ms!(100)),
    Tone::new(Hz!(1500), ms!(100)),
];

/// A specific protocol for encoding an image as a tone sequence.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum Mode {
    /// A 320x256 colour image in a 110 second transmission.
    Scottie1,
    /// A 320x256 colour image in a 71 second transmission.
    Scottie2,
    /// A 320x256 colour image in a 269 second transmission.
    ScottieDx,
    /// A 320x256 colour image in a 114 second transmission.
    Martin1,
    /// A 320x256 colour image in a 58 second transmission.
    Martin2,
    /// A 320x240 colour image in a 36 second transmission.
    Robot36,
    /// A 320x240 colour image in a 72 second transmission.
    Robot72,
    /// A 320x256 colour image in a 182 second transmission.
    WrasseSc2180,
    /// A 640x496 colour image in a 203 second transmission.
    PasokonP3,
    /// A 640x496 colour image in a 305 second transmission.
    PasokonP5,
    /// A 640x496 colour image in a 406 second transmission.
    PasokonP7,
    /// A 320x256 colour image in a 50 second transmission.
    Pd50,
    /// A 320x256 colour image in a 90 second transmission.
    Pd90,
    /// A 640x496 colour image in a 126 second transmission.
    Pd120,
    /// A 512x400 colour image in a 161 second transmission.
    Pd160,
    /// A 640x496 colour image in a 187 second transmission.
    Pd180,
    /// A 640x496 colour image in a 248 second transmission.
    Pd240,
    /// An 800x616 colour image in a 289 second transmission.
    Pd290,
    /// Robot B&W 8 s, 160x120. QSSTV VIS 0x82.
    RobotBw8,
    /// Robot B&W 12 s, 160x120. QSSTV VIS 0x86.
    RobotBw12,
    /// QSSTV Robot 24, 160x120 luminance. VIS 0x84.
    Robot24,
    /// Wraase SC2-30. Handbook 256x128 RGB. VIS 51.
    WrasseSc230,
    /// Wraase SC2-60. Handbook 256x256 RGB. QSSTV VIS 0xBB.
    WrasseSc260,
    /// Wraase SC2-120. Handbook 320x256 RGB. QSSTV VIS 0x3F.
    WrasseSc2120,
    /// AVT 24, 128x120 RGB, no line sync. QSSTV VIS 0xC0.
    Avt24,
    /// AVT 90, 256x240 RGB, no line sync. QSSTV VIS 0x44.
    Avt90,
    /// AVT 94, 320x200 RGB, no line sync. QSSTV VIS 0x48.
    Avt94,
    /// AVT 188, 320x400 RGB, no line sync. Manual or VIS 74.
    Avt188,
    /// When decoding, detect the transmission's mode from its header. When
    /// encoding, behaves as [`Robot36`](Mode::Robot36).
    Auto,
}

impl Mode {
    /// Every transmission mode, in the paper's order. Excludes [`Auto`](Mode::Auto).
    pub const ALL: [Self; 28] = [
        Self::Scottie1,
        Self::Scottie2,
        Self::ScottieDx,
        Self::Martin1,
        Self::Martin2,
        Self::Robot36,
        Self::Robot72,
        Self::WrasseSc2180,
        Self::PasokonP3,
        Self::PasokonP5,
        Self::PasokonP7,
        Self::Pd50,
        Self::Pd90,
        Self::Pd120,
        Self::Pd160,
        Self::Pd180,
        Self::Pd240,
        Self::Pd290,
        Self::RobotBw8,
        Self::RobotBw12,
        Self::Robot24,
        Self::WrasseSc230,
        Self::WrasseSc260,
        Self::WrasseSc2120,
        Self::Avt24,
        Self::Avt90,
        Self::Avt94,
        Self::Avt188,
    ];

    /// The mode's 7-bit VIS code, identifying it to a receiving system.
    #[must_use]
    pub const fn vis_code(&self) -> u8 {
        match self {
            Self::Auto => Self::Robot36.vis_code(),
            Self::Scottie1 => 60,
            Self::Scottie2 => 56,
            Self::ScottieDx => 76,
            Self::Martin1 => 44,
            Self::Martin2 => 40,
            Self::Robot36 => 8,
            Self::Robot72 => 12,
            Self::WrasseSc2180 => 55,
            Self::PasokonP3 => 113,
            Self::PasokonP5 => 114,
            Self::PasokonP7 => 115,
            Self::Pd50 => 93,
            Self::Pd90 => 99,
            Self::Pd120 => 95,
            Self::Pd160 => 98,
            Self::Pd180 => 96,
            Self::Pd240 => 97,
            Self::Pd290 => 94,
            Self::RobotBw8 => 2,
            Self::RobotBw12 => 6,
            Self::Robot24 => 4,
            Self::WrasseSc230 => 51,
            Self::WrasseSc260 => 59,
            Self::WrasseSc2120 => 63,
            Self::Avt24 => 64,
            Self::Avt90 => 68,
            Self::Avt94 => 72,
            Self::Avt188 => 74,
        }
    }

    /// Look up a mode by its 7-bit VIS code.
    #[must_use]
    pub const fn from_vis_code(code: u8) -> Option<Self> {
        match code {
            60 => Some(Self::Scottie1),
            56 => Some(Self::Scottie2),
            76 => Some(Self::ScottieDx),
            44 => Some(Self::Martin1),
            40 => Some(Self::Martin2),
            8 => Some(Self::Robot36),
            12 => Some(Self::Robot72),
            55 => Some(Self::WrasseSc2180),
            113 => Some(Self::PasokonP3),
            114 => Some(Self::PasokonP5),
            115 => Some(Self::PasokonP7),
            93 => Some(Self::Pd50),
            99 => Some(Self::Pd90),
            95 => Some(Self::Pd120),
            98 => Some(Self::Pd160),
            96 => Some(Self::Pd180),
            97 => Some(Self::Pd240),
            94 => Some(Self::Pd290),
            2 => Some(Self::RobotBw8),
            6 => Some(Self::RobotBw12),
            4 => Some(Self::Robot24),
            51 => Some(Self::WrasseSc230),
            59 => Some(Self::WrasseSc260),
            63 => Some(Self::WrasseSc2120),
            64 => Some(Self::Avt24),
            68 => Some(Self::Avt90),
            72 => Some(Self::Avt94),
            74 => Some(Self::Avt188),
            _ => None,
        }
    }

    /// The mode's scanline structure as specified by its timing-sequence
    /// table in the paper.
    pub(crate) const fn layout(self) -> Layout {
        match self {
            Self::Auto => Self::Robot36.layout(),
            Self::Scottie1 => scottie::SCOTTIE_1,
            Self::Scottie2 => scottie::SCOTTIE_2,
            Self::ScottieDx => scottie::SCOTTIE_DX,
            Self::Martin1 => martin::MARTIN_1,
            Self::Martin2 => martin::MARTIN_2,
            Self::Robot36 => robot::ROBOT_36,
            Self::Robot72 => robot::ROBOT_72,
            Self::WrasseSc2180 => wrasse::WRASSE_SC2_180,
            Self::PasokonP3 => pasokon::PASOKON_P3,
            Self::PasokonP5 => pasokon::PASOKON_P5,
            Self::PasokonP7 => pasokon::PASOKON_P7,
            Self::Pd50 => pd::PD_50,
            Self::Pd90 => pd::PD_90,
            Self::Pd120 => pd::PD_120,
            Self::Pd160 => pd::PD_160,
            Self::Pd180 => pd::PD_180,
            Self::Pd240 => pd::PD_240,
            Self::Pd290 => pd::PD_290,
            Self::RobotBw8 => extra::ROBOT_BW8,
            Self::RobotBw12 => extra::ROBOT_BW12,
            Self::Robot24 => extra::ROBOT_24,
            Self::WrasseSc230 => extra::WRASSE_SC2_30,
            Self::WrasseSc260 => extra::WRASSE_SC2_60,
            Self::WrasseSc2120 => extra::WRASSE_SC2_120,
            Self::Avt24 => extra::AVT_24,
            Self::Avt90 => extra::AVT_90,
            Self::Avt94 => extra::AVT_94,
            Self::Avt188 => extra::AVT_188,
        }
    }

    /// The horizontal resolution in pixels.
    #[must_use]
    pub const fn image_width(&self) -> u32 {
        self.layout().width as u32
    }

    /// The vertical resolution in pixels.
    #[must_use]
    pub const fn image_height(&self) -> u32 {
        self.layout().height as u32
    }

    /// Whether the mode transmits one extra sync pulse between the header and
    /// the first line. Only Scottie modes do.
    pub(crate) const fn has_starting_sync_pulse(self) -> bool {
        matches!(self, Self::Scottie1 | Self::Scottie2 | Self::ScottieDx)
    }

    /// The tones sent before the image: the VOX tuning tones, the calibration
    /// header carrying the VIS code, and the starting sync pulse for modes
    /// that transmit one. The image data begins immediately after the last
    /// header tone.
    pub fn header_tones(&self) -> impl Iterator<Item = Tone> + '_ {
        (0..).map_while(move |index| self.header_tone(index))
    }

    /// The `index`-th header tone, or `None` past the end of the header.
    pub(crate) fn header_tone(self, index: usize) -> Option<Tone> {
        let code = self.vis_code();
        let bit = |one: bool| {
            let frequency = if one {
                VIS_ONE_FREQUENCY
            } else {
                VIS_ZERO_FREQUENCY
            };
            Tone::new(frequency, VIS_BIT_DURATION)
        };
        match index {
            0..=7 => Some(VOX_TONES[index]),
            8 | 10 => Some(Tone::new(LEADER_FREQUENCY, ms!(300))),
            9 => Some(Tone::new(SYNC_FREQUENCY, ms!(10))), // break
            11 | 20 => Some(Tone::new(SYNC_FREQUENCY, VIS_BIT_DURATION)), // start and stop bits
            12..=18 => Some(bit((code >> (index - 12)) & 1 == 1)), // code bits, least significant first
            19 => Some(bit(code.count_ones() % 2 == 1)),           // even parity
            21 if self.has_starting_sync_pulse() => {
                Some(Tone::new(SYNC_FREQUENCY, self.layout().sync_pulse().1))
            }
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec::Vec;

    use super::*;
    use crate::synthesizer::Tone;
    use crate::{Hz, ms, us};

    #[test]
    fn header_tones_robot36() {
        assert_eq!(
            Mode::Robot36.header_tones().collect::<Vec<_>>(),
            std::vec![
                Tone::new(Hz!(1900), ms!(100)),
                Tone::new(Hz!(1500), ms!(100)),
                Tone::new(Hz!(1900), ms!(100)),
                Tone::new(Hz!(1500), ms!(100)),
                Tone::new(Hz!(2300), ms!(100)),
                Tone::new(Hz!(1500), ms!(100)),
                Tone::new(Hz!(2300), ms!(100)),
                Tone::new(Hz!(1500), ms!(100)),
                Tone::new(Hz!(1900), ms!(300)),
                Tone::new(Hz!(1200), ms!(10)),
                Tone::new(Hz!(1900), ms!(300)),
                Tone::new(Hz!(1200), ms!(30)),
                Tone::new(Hz!(1300), ms!(30)),
                Tone::new(Hz!(1300), ms!(30)),
                Tone::new(Hz!(1300), ms!(30)),
                Tone::new(Hz!(1100), ms!(30)),
                Tone::new(Hz!(1300), ms!(30)),
                Tone::new(Hz!(1300), ms!(30)),
                Tone::new(Hz!(1300), ms!(30)),
                Tone::new(Hz!(1100), ms!(30)),
                Tone::new(Hz!(1200), ms!(30)),
            ]
        );
    }

    #[test]
    fn vis_codes_round_trip() {
        for mode in Mode::ALL {
            let code = mode.vis_code();
            assert_eq!(Mode::from_vis_code(code), Some(mode));
            assert!(code < 128, "VIS codes are 7 bit");
        }
    }

    /// Every sequence of a mode must be equally long — the decoder relies on
    /// the sync pulses being evenly spaced.
    #[test]
    fn sequences_are_equally_long() {
        for mode in Mode::ALL {
            let layout = mode.layout();
            let duration = layout.sequence_duration();
            for sequence in layout.sequences {
                let sum = sequence
                    .iter()
                    .fold(us!(0), |sum, step| sum + step.duration());
                assert_eq!(sum, duration, "{mode:?}");
            }
        }
    }

    /// The per-line duration from the paper: Robot 36 transmits 240 lines in
    /// 36 seconds — 150.0ms per line.
    #[test]
    fn robot36_line_duration_matches_paper() {
        assert_eq!(Mode::Robot36.layout().sequence_duration(), ms!(150));
    }

    /// The paper publishes each mode's total transmission time (excluding the
    /// header) alongside the per-step timings. Summing our transcribed steps
    /// over all lines must reproduce those times, which catches transcription
    /// mistakes in any single step.
    #[test]
    fn transmission_times_match_paper() {
        let expected_seconds = [
            (Mode::Scottie1, 109.6),
            (Mode::Scottie2, 71.1),
            (Mode::ScottieDx, 268.9),
            (Mode::Martin1, 114.3),
            (Mode::Martin2, 58.06),
            (Mode::Robot36, 36.0),
            (Mode::Robot72, 72.0),
            (Mode::WrasseSc2180, 182.0),
            (Mode::PasokonP3, 203.0),
            (Mode::PasokonP5, 304.6),
            (Mode::PasokonP7, 406.1),
            (Mode::Pd50, 49.7),
            (Mode::Pd90, 90.0),
            (Mode::Pd120, 126.1),
            (Mode::Pd160, 160.9),
            (Mode::Pd180, 187.1),
            (Mode::Pd240, 248.0),
            (Mode::Pd290, 288.7),
        ];
        for (mode, expected) in expected_seconds {
            let layout = mode.layout();
            let passes = (layout.height / layout.lines_per_sequence) as f64;
            let seconds = passes * layout.sequence_duration().ns() as f64 / 1e9;
            assert!(
                (seconds - expected).abs() < 0.1,
                "{mode:?}: {seconds}s instead of {expected}s",
            );
        }
    }
}
