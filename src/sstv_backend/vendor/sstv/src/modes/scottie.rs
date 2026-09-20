//! Scottie modes.
//!
//! Scottie modes transmit green, blue and red scans of every line. They are
//! unusual in two ways: the sync pulse sits between the blue and the red scan
//! instead of at the line break, and a single extra sync pulse precedes the
//! very first line (emitted here as the last header tone).

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::units::Duration;
use crate::{Hz, ms, us};

const SYNC_PULSE: Step = Step::tone(Hz!(1200), ms!(9));
const SYNC_PORCH: Step = Step::tone(Hz!(1500), us!(1_500));
const SEPARATOR_PULSE: Step = Step::tone(Hz!(1500), us!(1_500));

const fn sequence(scan: Duration) -> [Step; 7] {
    [
        SEPARATOR_PULSE,
        Step::scan(Channel::Green, scan),
        SEPARATOR_PULSE,
        Step::scan(Channel::Blue, scan),
        SYNC_PULSE,
        SYNC_PORCH,
        Step::scan(Channel::Red, scan),
    ]
}

const SCOTTIE_1_SEQUENCE: [Step; 7] = sequence(us!(138_240));
const SCOTTIE_2_SEQUENCE: [Step; 7] = sequence(us!(88_064));
const SCOTTIE_DX_SEQUENCE: [Step; 7] = sequence(us!(345_600));

const fn layout(width: usize, height: usize, sequences: &'static [&'static [Step]]) -> Layout {
    Layout {
        width,
        height,
        sequences,
        lines_per_sequence: 1,
        color: ColorMode::Rgb,
    }
}

/// 256 lines of 428.22ms each: a 110 second transmission.
pub const SCOTTIE_1: Layout = layout(320, 256, &[&SCOTTIE_1_SEQUENCE]);
/// Dayton: 320x256 at S2 scan time (71 s).
pub const SCOTTIE_2: Layout = layout(320, 256, &[&SCOTTIE_2_SEQUENCE]);
/// 256 lines of 1050.3ms each: a 269 second transmission.
pub const SCOTTIE_DX: Layout = layout(320, 256, &[&SCOTTIE_DX_SEQUENCE]);
/// Handbook S3: S1 scans, 128 lines, ~55 s. VIS 52.
pub const SCOTTIE_3: Layout = layout(320, 128, &[&SCOTTIE_1_SEQUENCE]);
/// Handbook S4: S2 scans, 160x128, ~36 s. VIS 48.
pub const SCOTTIE_4: Layout = layout(160, 128, &[&SCOTTIE_2_SEQUENCE]);
