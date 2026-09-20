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
    /// Handbook Martin M3: 320x128, M1 scans, VIS 36.
    Martin3,
    /// Handbook Martin M4: 160x128, M2 scans, VIS 32.
    Martin4,
    /// Handbook Scottie S3: 320x128, S1 scans, VIS 52.
    Scottie3,
    /// Handbook Scottie S4: 160x128, S2 scans, VIS 48.
    Scottie4,
    /// QSSTV FAX480: 512x500 luma, no VIS.
    Fax480,
    /// Handbook Wraase SC-1 24 colour, VIS 16.
    WrasseSc124,
    /// Handbook Wraase SC-1 48 colour, VIS 20.
    WrasseSc148,
    /// Handbook Wraase SC-1 48Q colour, VIS 24.
    WrasseSc148q,
    /// Handbook Wraase SC-1 96 colour, VIS 28.
    WrasseSc196,
    /// QSSTV MP73 (PD family, 16-bit VIS 0x2523).
    Mp73,
    /// QSSTV MP115 (PD family, 16-bit VIS 0x2923).
    Mp115,
    /// QSSTV MP140 (PD family, 16-bit VIS 0x2A23).
    Mp140,
    /// QSSTV MP175 (PD family, 16-bit VIS 0x2C23).
    Mp175,
    /// QSSTV MR73 (Robot-2 family, 16-bit VIS 0x4523).
    Mr73,
    /// QSSTV MR90 (Robot-2 family, 16-bit VIS 0x4623).
    Mr90,
    /// QSSTV MR115 (Robot-2 family, 16-bit VIS 0x4923).
    Mr115,
    /// QSSTV MR140 (Robot-2 family, 16-bit VIS 0x4A23).
    Mr140,
    /// QSSTV ML180 (Robot-2 family, 16-bit VIS 0x8523).
    Ml180,
    /// QSSTV ML240 (Robot-2 family, 16-bit VIS 0x8623).
    Ml240,
    /// QSSTV ML280 (Robot-2 family, 16-bit VIS 0x8923).
    Ml280,
    /// QSSTV ML320 (Robot-2 family, 16-bit VIS 0x8A23).
    Ml320,
    /// When decoding, detect the transmission's mode from its header. When
    /// encoding, behaves as [`Robot36`](Mode::Robot36).
    Auto,
}

impl Mode {
    /// Every transmission mode. Excludes [`Auto`](Mode::Auto).
    /// 256-line / common variants come before same-period half-height twins so
    /// line-sync Auto prefers the fuller picture when VIS is missing.
    pub const ALL: [Self; 49] = [
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
        Self::Martin3,
        Self::Martin4,
        Self::Scottie3,
        Self::Scottie4,
        Self::WrasseSc148,
        Self::WrasseSc196,
        Self::WrasseSc124,
        Self::WrasseSc148q,
        Self::Fax480,
        Self::Mp73,
        Self::Mp115,
        Self::Mp140,
        Self::Mp175,
        Self::Mr73,
        Self::Mr90,
        Self::Mr115,
        Self::Mr140,
        Self::Ml180,
        Self::Ml240,
        Self::Ml280,
        Self::Ml320,
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
            Self::Martin3 => 36,
            Self::Martin4 => 32,
            Self::Scottie3 => 52,
            Self::Scottie4 => 48,
            Self::WrasseSc124 => 16,
            Self::WrasseSc148 => 20,
            Self::WrasseSc148q => 24,
            Self::WrasseSc196 => 28,
            // FAX480 has no VIS. 16-bit MP/MR/ML words are not 7-bit codes.
            Self::Fax480
            | Self::Mp73
            | Self::Mp115
            | Self::Mp140
            | Self::Mp175
            | Self::Mr73
            | Self::Mr90
            | Self::Mr115
            | Self::Mr140
            | Self::Ml180
            | Self::Ml240
            | Self::Ml280
            | Self::Ml320 => 0,
        }
    }

    /// QSSTV 16-bit VIS word (`0xNN23`), or `None` for 7-bit / no-VIS modes.
    #[must_use]
    pub const fn vis_word(self) -> Option<u16> {
        match self {
            Self::Mp73 => Some(0x2523),
            Self::Mp115 => Some(0x2923),
            Self::Mp140 => Some(0x2A23),
            Self::Mp175 => Some(0x2C23),
            Self::Mr73 => Some(0x4523),
            Self::Mr90 => Some(0x4623),
            Self::Mr115 => Some(0x4923),
            Self::Mr140 => Some(0x4A23),
            Self::Ml180 => Some(0x8523),
            Self::Ml240 => Some(0x8623),
            Self::Ml280 => Some(0x8923),
            Self::Ml320 => Some(0x8A23),
            _ => None,
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
            36 => Some(Self::Martin3),
            32 => Some(Self::Martin4),
            52 => Some(Self::Scottie3),
            48 => Some(Self::Scottie4),
            16 => Some(Self::WrasseSc124),
            20 => Some(Self::WrasseSc148),
            24 => Some(Self::WrasseSc148q),
            28 => Some(Self::WrasseSc196),
            _ => None,
        }
    }

    /// Look up a QSSTV 16-bit VIS word. Low byte must be `0x23`.
    #[must_use]
    pub const fn from_vis_word(code: u16) -> Option<Self> {
        match code {
            0x2523 => Some(Self::Mp73),
            0x2923 => Some(Self::Mp115),
            0x2A23 => Some(Self::Mp140),
            0x2C23 => Some(Self::Mp175),
            0x4523 => Some(Self::Mr73),
            0x4623 => Some(Self::Mr90),
            0x4923 => Some(Self::Mr115),
            0x4A23 => Some(Self::Mr140),
            0x8523 => Some(Self::Ml180),
            0x8623 => Some(Self::Ml240),
            0x8923 => Some(Self::Ml280),
            0x8A23 => Some(Self::Ml320),
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
            Self::Martin3 => martin::MARTIN_3,
            Self::Martin4 => martin::MARTIN_4,
            Self::Scottie3 => scottie::SCOTTIE_3,
            Self::Scottie4 => scottie::SCOTTIE_4,
            Self::Fax480 => extra::FAX_480,
            Self::WrasseSc124 => extra::SC1_24,
            Self::WrasseSc148 => extra::SC1_48,
            Self::WrasseSc148q => extra::SC1_48Q,
            Self::WrasseSc196 => extra::SC1_96,
            Self::Mp73 => extra::MP_73,
            Self::Mp115 => extra::MP_115,
            Self::Mp140 => extra::MP_140,
            Self::Mp175 => extra::MP_175,
            Self::Mr73 => extra::MR_73,
            Self::Mr90 => extra::MR_90,
            Self::Mr115 => extra::MR_115,
            Self::Mr140 => extra::MR_140,
            Self::Ml180 => extra::ML_180,
            Self::Ml240 => extra::ML_240,
            Self::Ml280 => extra::ML_280,
            Self::Ml320 => extra::ML_320,
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
        matches!(
            self,
            Self::Scottie1 | Self::Scottie2 | Self::ScottieDx | Self::Scottie3 | Self::Scottie4
        )
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
        let bit = |one: bool| {
            let frequency = if one {
                VIS_ONE_FREQUENCY
            } else {
                VIS_ZERO_FREQUENCY
            };
            Tone::new(frequency, VIS_BIT_DURATION)
        };
        match index {
            0..=7 => return Some(VOX_TONES[index]),
            8 | 10 => return Some(Tone::new(LEADER_FREQUENCY, ms!(300))),
            9 => return Some(Tone::new(SYNC_FREQUENCY, ms!(10))), // break
            _ => {}
        }
        // FAX480: QSSTV sends no VIS. Image follows the second leader.
        if self.vis_code() == 0 && self.vis_word().is_none() {
            return if index == 11 && self.has_starting_sync_pulse() {
                Some(Tone::new(SYNC_FREQUENCY, self.layout().sync_pulse().1))
            } else {
                None
            };
        }
        if let Some(word) = self.vis_word() {
            // QSSTV 16-bit VIS: start + 16 data bits + stop (no extra parity bit).
            return match index {
                11 => Some(Tone::new(SYNC_FREQUENCY, VIS_BIT_DURATION)),
                12..=27 => Some(bit((word >> (index - 12)) & 1 == 1)),
                28 => Some(Tone::new(SYNC_FREQUENCY, VIS_BIT_DURATION)),
                29 if self.has_starting_sync_pulse() => {
                    Some(Tone::new(SYNC_FREQUENCY, self.layout().sync_pulse().1))
                }
                _ => None,
            };
        }
        let code = self.vis_code();
        match index {
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
            if let Some(word) = mode.vis_word() {
                assert_eq!(Mode::from_vis_word(word), Some(mode));
                assert_eq!(mode.vis_code(), 0);
                continue;
            }
            let code = mode.vis_code();
            if code == 0 {
                assert_eq!(Mode::from_vis_code(0), None, "{mode:?} has no 7-bit VIS");
                continue;
            }
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
            (Mode::Martin3, 57.1),
            (Mode::Martin4, 29.0),
            (Mode::Scottie3, 54.8),
            (Mode::Scottie4, 35.5),
            (Mode::Fax480, 133.6),
            (Mode::WrasseSc124, 23.0),
            (Mode::WrasseSc148, 46.1),
            (Mode::WrasseSc148q, 43.8),
            (Mode::WrasseSc196, 87.6),
            (Mode::Mp73, 73.0),
            (Mode::Mp115, 115.5),
            (Mode::Mp140, 139.5),
            (Mode::Mp175, 175.4),
            (Mode::Mr73, 73.3),
            (Mode::Mr90, 90.2),
            (Mode::Mr115, 115.3),
            (Mode::Mr140, 140.4),
            (Mode::Ml180, 180.2),
            (Mode::Ml240, 239.7),
            (Mode::Ml280, 280.4),
            (Mode::Ml320, 320.1),
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
