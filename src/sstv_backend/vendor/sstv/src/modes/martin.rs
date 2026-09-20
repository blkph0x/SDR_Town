//! Martin modes.
//!
//! Martin modes transmit green, blue and red scans of every line, with the
//! sync pulse at the line break and a short separator pulse after each scan.

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::units::Duration;
use crate::{Hz, us};

const SYNC_PULSE: Step = Step::tone(Hz!(1200), us!(4_862));
const SYNC_PORCH: Step = Step::tone(Hz!(1500), us!(572));
const SEPARATOR_PULSE: Step = Step::tone(Hz!(1500), us!(572));

const fn sequence(scan: Duration) -> [Step; 8] {
    [
        SYNC_PULSE,
        SYNC_PORCH,
        Step::scan(Channel::Green, scan),
        SEPARATOR_PULSE,
        Step::scan(Channel::Blue, scan),
        SEPARATOR_PULSE,
        Step::scan(Channel::Red, scan),
        SEPARATOR_PULSE,
    ]
}

const MARTIN_1_SEQUENCE: [Step; 8] = sequence(us!(146_432));
const MARTIN_2_SEQUENCE: [Step; 8] = sequence(us!(73_216));

const fn layout(sequences: &'static [&'static [Step]]) -> Layout {
    Layout {
        width: 320,
        height: 256,
        sequences,
        lines_per_sequence: 1,
        color: ColorMode::Rgb,
    }
}

/// 256 lines of 446.446ms each: a 114 second transmission.
pub const MARTIN_1: Layout = layout(&[&MARTIN_1_SEQUENCE]);
/// 256 lines of 226.798ms each: a 58 second transmission.
pub const MARTIN_2: Layout = layout(&[&MARTIN_2_SEQUENCE]);
