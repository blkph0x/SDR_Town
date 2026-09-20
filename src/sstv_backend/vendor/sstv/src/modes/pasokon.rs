//! Pasokon modes.
//!
//! High-resolution modes transmitting red, green and blue scans of every
//! line, with a porch after every scan. Sync pulse and porch lengths vary
//! with the sub-mode: they were chosen to divide evenly into standard RS232
//! clock rates.

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::units::Duration;
use crate::{Hz, us};

const fn sequence(sync: Duration, porch: Duration, scan: Duration) -> [Step; 8] {
    [
        Step::tone(Hz!(1200), sync),
        Step::tone(Hz!(1500), porch),
        Step::scan(Channel::Red, scan),
        Step::tone(Hz!(1500), porch),
        Step::scan(Channel::Green, scan),
        Step::tone(Hz!(1500), porch),
        Step::scan(Channel::Blue, scan),
        Step::tone(Hz!(1500), porch),
    ]
}

const P3_SEQUENCE: [Step; 8] = sequence(us!(5_208), us!(1_042), us!(133_333));
const P5_SEQUENCE: [Step; 8] = sequence(us!(7_813), us!(1_563), us!(200_000));
const P7_SEQUENCE: [Step; 8] = sequence(us!(10_417), us!(2_083), us!(266_666));

const fn layout(sequences: &'static [&'static [Step]]) -> Layout {
    Layout {
        width: 640,
        height: 496,
        sequences,
        lines_per_sequence: 1,
        color: ColorMode::Rgb,
    }
}

/// 496 lines of 409.375ms each: a 203 second transmission.
pub const PASOKON_P3: Layout = layout(&[&P3_SEQUENCE]);
/// 496 lines of 614.065ms each: a 305 second transmission.
pub const PASOKON_P5: Layout = layout(&[&P5_SEQUENCE]);
/// 496 lines of 818.747ms each: a 406 second transmission.
pub const PASOKON_P7: Layout = layout(&[&P7_SEQUENCE]);
