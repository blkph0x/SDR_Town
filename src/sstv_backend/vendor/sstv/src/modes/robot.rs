//! Robot modes.
//!
//! Robot modes encode colour as luminance plus two colour differences, with
//! the colour differences scanned at half the luminance scan period. Robot 36
//! halves the colour information further by averaging it over a line pair:
//! even lines carry the red difference, odd lines the blue one, with a
//! separator pulse marking the parity (1500hz on even lines, 2300hz on odd
//! lines). Robot 72 transmits both differences on every line.

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::{Hz, ms, us};

const SYNC_PULSE: Step = Step::tone(Hz!(1200), ms!(9));
const SYNC_PORCH: Step = Step::tone(Hz!(1500), ms!(3));
const PORCH: Step = Step::tone(Hz!(1900), us!(1_500));

/// The even line of a pair.
const ROBOT_36_EVEN_SEQUENCE: [Step; 6] = [
    SYNC_PULSE,
    SYNC_PORCH,
    Step::scan(Channel::Y, ms!(88)),
    Step::tone(Hz!(1500), us!(4_500)), // even-line separator pulse
    PORCH,
    Step::scan(Channel::RY, ms!(44)),
];

/// The odd line of a pair.
const ROBOT_36_ODD_SEQUENCE: [Step; 6] = [
    SYNC_PULSE,
    SYNC_PORCH,
    Step::scan(Channel::Y, ms!(88)),
    Step::tone(Hz!(2300), us!(4_500)), // odd-line separator pulse
    PORCH,
    Step::scan(Channel::BY, ms!(44)),
];

/// 240 lines of 150ms each: a 36 second transmission.
pub const ROBOT_36: Layout = Layout {
    width: 320,
    height: 240,
    sequences: &[&ROBOT_36_EVEN_SEQUENCE, &ROBOT_36_ODD_SEQUENCE],
    lines_per_sequence: 1,
    color: ColorMode::YuvAveragedPair,
};

const ROBOT_72_SEQUENCE: [Step; 9] = [
    SYNC_PULSE,
    SYNC_PORCH,
    Step::scan(Channel::Y, ms!(138)),
    Step::tone(Hz!(1500), us!(4_500)), // even separator pulse
    PORCH,
    Step::scan(Channel::RY, ms!(69)),
    Step::tone(Hz!(2300), us!(4_500)), // odd separator pulse
    Step::tone(Hz!(1500), us!(1_500)),
    Step::scan(Channel::BY, ms!(69)),
];

/// 240 lines of 300ms each: a 72 second transmission.
pub const ROBOT_72: Layout = Layout {
    width: 320,
    height: 240,
    sequences: &[&ROBOT_72_SEQUENCE],
    lines_per_sequence: 1,
    color: ColorMode::Yuv,
};
