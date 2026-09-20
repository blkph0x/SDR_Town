//! Extra modes transcribed from QSSTV `sstvparam.cpp` VIS/geometry and
//! SSTV Handbook ch.4 line timings (SC2-30/60/120, AVT RGB). Not in the
//! original Dayton-paper crate set.

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::{Hz, ms, us};

// Robot B&W 8: handbook 160x120, sync 10ms + Y 56ms. QSSTV VIS 0x82 → 7-bit 2.
const ROBOT_BW8_SEQ: [Step; 2] = [
    Step::tone(Hz!(1200), ms!(10)),
    Step::scan(Channel::Y, ms!(56)),
];
pub const ROBOT_BW8: Layout = Layout {
    width: 160,
    height: 120,
    sequences: &[&ROBOT_BW8_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Luma,
};

// Robot B&W 12: handbook 160x120, sync 7ms + Y 93ms. QSSTV VIS 0x86 → 7-bit 6.
const ROBOT_BW12_SEQ: [Step; 2] = [
    Step::tone(Hz!(1200), ms!(7)),
    Step::scan(Channel::Y, ms!(93)),
];
pub const ROBOT_BW12: Layout = Layout {
    width: 160,
    height: 120,
    sequences: &[&ROBOT_BW12_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Luma,
};

// QSSTV Robot 24 colour geometry 160x120, 24.0015s, VIS 0x84 → 7-bit 4.
// Channel table is not in Dayton; decode luminance of the line body.
const ROBOT_24_SEQ: [Step; 3] = [
    Step::tone(Hz!(1200), ms!(6)),
    Step::tone(Hz!(1500), us!(100)),
    Step::scan(Channel::Y, us!(193_913)),
];
pub const ROBOT_24: Layout = Layout {
    width: 160,
    height: 120,
    sequences: &[&ROBOT_24_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Luma,
};

// Handbook SC-2 30: 256x128 RGB 2:4:2, sync 5ms. VIS 0x33 → 51 (WB2OSZ SC-2 30).
const SC2_30_SEQ: [Step; 4] = [
    Step::tone(Hz!(1200), ms!(5)),
    Step::scan(Channel::Red, ms!(58)),
    Step::scan(Channel::Green, ms!(117)),
    Step::scan(Channel::Blue, ms!(58)),
];
pub const WRASSE_SC2_30: Layout = Layout {
    width: 256,
    height: 128,
    sequences: &[&SC2_30_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

// Handbook SC-2 60: 256x256 same line as 30. QSSTV uses 320x256; keep handbook
// 256 wide to match the 58/117/58 ms scans. VIS 0xBB → 59.
const SC2_60_SEQ: [Step; 4] = [
    Step::tone(Hz!(1200), ms!(5)),
    Step::scan(Channel::Red, ms!(58)),
    Step::scan(Channel::Green, ms!(117)),
    Step::scan(Channel::Blue, ms!(58)),
];
pub const WRASSE_SC2_60: Layout = Layout {
    width: 256,
    height: 256,
    sequences: &[&SC2_60_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

// Handbook SC-2 120: 320x256 RGB 117/235/117 ms, sync 5ms. VIS 0x3F → 63.
const SC2_120_SEQ: [Step; 4] = [
    Step::tone(Hz!(1200), ms!(5)),
    Step::scan(Channel::Red, ms!(117)),
    Step::scan(Channel::Green, ms!(235)),
    Step::scan(Channel::Blue, ms!(117)),
];
pub const WRASSE_SC2_120: Layout = Layout {
    width: 320,
    height: 256,
    sequences: &[&SC2_120_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

// AVT: no line sync (handbook 4.2.5). Equal RGB scans. Auto requires VIS.
const AVT24_SEQ: [Step; 3] = [
    Step::scan(Channel::Red, us!(62_500)),
    Step::scan(Channel::Green, us!(62_500)),
    Step::scan(Channel::Blue, us!(62_500)),
];
pub const AVT_24: Layout = Layout {
    width: 128,
    height: 120,
    sequences: &[&AVT24_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const AVT90_SEQ: [Step; 3] = [
    Step::scan(Channel::Red, ms!(125)),
    Step::scan(Channel::Green, ms!(125)),
    Step::scan(Channel::Blue, ms!(125)),
];
pub const AVT_90: Layout = Layout {
    width: 256,
    height: 240,
    sequences: &[&AVT90_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const AVT94_SEQ: [Step; 3] = [
    Step::scan(Channel::Red, us!(156_250)),
    Step::scan(Channel::Green, us!(156_250)),
    Step::scan(Channel::Blue, us!(156_250)),
];
pub const AVT_94: Layout = Layout {
    width: 320,
    height: 200,
    sequences: &[&AVT94_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const AVT188_SEQ: [Step; 3] = [
    Step::scan(Channel::Red, us!(156_250)),
    Step::scan(Channel::Green, us!(156_250)),
    Step::scan(Channel::Blue, us!(156_250)),
];
pub const AVT_188: Layout = Layout {
    width: 320,
    height: 400,
    sequences: &[&AVT188_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};
