//! PD modes.
//!
//! PD modes transmit two image lines per sequence: the first line's
//! luminance, the red and blue colour differences averaged over both lines,
//! and then the second line's luminance. The sub-modes differ only in scan
//! time and resolution.

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::units::Duration;
use crate::{Hz, ms, us};

const SYNC_PULSE: Step = Step::tone(Hz!(1200), ms!(20));
const PORCH: Step = Step::tone(Hz!(1500), us!(2_080));

const fn sequence(scan: Duration) -> [Step; 6] {
    [
        SYNC_PULSE,
        PORCH,
        Step::scan(Channel::Y, scan),
        Step::scan(Channel::RY, scan),
        Step::scan(Channel::BY, scan),
        Step::scan(Channel::YSecond, scan),
    ]
}

const PD_50_SEQUENCE: [Step; 6] = sequence(us!(91_520));
const PD_90_SEQUENCE: [Step; 6] = sequence(us!(170_240));
const PD_120_SEQUENCE: [Step; 6] = sequence(us!(121_600));
const PD_160_SEQUENCE: [Step; 6] = sequence(us!(195_584));
const PD_180_SEQUENCE: [Step; 6] = sequence(us!(183_040));
const PD_240_SEQUENCE: [Step; 6] = sequence(us!(244_480));
const PD_290_SEQUENCE: [Step; 6] = sequence(us!(228_800));

const fn layout(width: usize, height: usize, sequences: &'static [&'static [Step]]) -> Layout {
    Layout {
        width,
        height,
        sequences,
        lines_per_sequence: 2,
        color: ColorMode::YuvSharedPair,
    }
}

/// 128 line pairs of 388.16ms each: a 50 second transmission.
pub const PD_50: Layout = layout(320, 256, &[&PD_50_SEQUENCE]);
/// 128 line pairs of 703.04ms each: a 90 second transmission.
pub const PD_90: Layout = layout(320, 256, &[&PD_90_SEQUENCE]);
/// 248 line pairs of 508.48ms each: a 126 second transmission.
pub const PD_120: Layout = layout(640, 496, &[&PD_120_SEQUENCE]);
/// 200 line pairs of 804.416ms each: a 161 second transmission.
pub const PD_160: Layout = layout(512, 400, &[&PD_160_SEQUENCE]);
/// 248 line pairs of 754.24ms each: a 187 second transmission.
pub const PD_180: Layout = layout(640, 496, &[&PD_180_SEQUENCE]);
/// 248 line pairs of 1000ms each: a 248 second transmission.
pub const PD_240: Layout = layout(640, 496, &[&PD_240_SEQUENCE]);
/// 308 line pairs of 937.28ms each: a 289 second transmission.
pub const PD_290: Layout = layout(800, 616, &[&PD_290_SEQUENCE]);
