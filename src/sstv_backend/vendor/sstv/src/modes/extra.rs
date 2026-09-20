//! Extra modes transcribed from QSSTV `sstvparam.cpp` VIS/geometry and
//! SSTV Handbook ch.4 line timings (SC2-30/60/120, AVT RGB). Not in the
//! original Dayton-paper crate set.

use super::layout::{Channel, ColorMode, Layout, Step};
use crate::units::Duration;
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

// QSSTV FAX480: 512x500, 133.633 s, sync 5.12 ms, no VIS (code 0). Luma scan.
const FAX480_SEQ: [Step; 2] = [
    Step::tone(Hz!(1200), us!(5_120)),
    Step::scan(Channel::Y, us!(262_146)),
];
pub const FAX_480: Layout = Layout {
    width: 512,
    height: 500,
    sequences: &[&FAX480_SEQ],
    lines_per_sequence: 1,
    color: ColorMode::Luma,
};

// Handbook table 4.2: SC-1, 6 ms 1200 Hz sync before each G/B/R scan.
const fn sc1_sequence(scan: Duration) -> [Step; 6] {
    [
        Step::tone(Hz!(1200), ms!(6)),
        Step::scan(Channel::Green, scan),
        Step::tone(Hz!(1200), ms!(6)),
        Step::scan(Channel::Blue, scan),
        Step::tone(Hz!(1200), ms!(6)),
        Step::scan(Channel::Red, scan),
    ]
}

const SC1_24_SEQ: [Step; 6] = sc1_sequence(ms!(54));
const SC1_48_SEQ: [Step; 6] = sc1_sequence(ms!(54));
const SC1_48Q_SEQ: [Step; 6] = sc1_sequence(ms!(108));
const SC1_96_SEQ: [Step; 6] = sc1_sequence(ms!(108));

const fn rgb_layout(
    width: usize,
    height: usize,
    sequences: &'static [&'static [Step]],
) -> Layout {
    Layout {
        width,
        height,
        sequences,
        lines_per_sequence: 1,
        color: ColorMode::Rgb,
    }
}

/// Handbook SC-1 24 colour: 128x128, VIS 16.
pub const SC1_24: Layout = rgb_layout(128, 128, &[&SC1_24_SEQ]);
/// Handbook SC-1 48 colour: 128x256, VIS 20.
pub const SC1_48: Layout = rgb_layout(128, 256, &[&SC1_48_SEQ]);
/// Handbook SC-1 48Q colour: 256x128, VIS 24.
pub const SC1_48Q: Layout = rgb_layout(256, 128, &[&SC1_48Q_SEQ]);
/// Handbook SC-1 96 colour: 256x256, VIS 28.
pub const SC1_96: Layout = rgb_layout(256, 256, &[&SC1_96_SEQ]);

// QSSTV modePD (MP*): Y, R-Y, B-Y, Y2, then 9 ms sync and 1 ms porch.
// Scan duration = (imageTime/dataLines - sync - porch) / 4.
const fn mp_sequence(scan: Duration) -> [Step; 6] {
    [
        Step::scan(Channel::Y, scan),
        Step::scan(Channel::RY, scan),
        Step::scan(Channel::BY, scan),
        Step::scan(Channel::YSecond, scan),
        Step::tone(Hz!(1200), ms!(9)),
        Step::tone(Hz!(1500), ms!(1)),
    ]
}

const fn pd_layout(
    width: usize,
    height: usize,
    sequences: &'static [&'static [Step]],
) -> Layout {
    Layout {
        width,
        height,
        sequences,
        lines_per_sequence: 2,
        color: ColorMode::YuvSharedPair,
    }
}

const MP73_SEQ: [Step; 6] = mp_sequence(us!(140_008));
const MP115_SEQ: [Step; 6] = mp_sequence(us!(223_016));
const MP140_SEQ: [Step; 6] = mp_sequence(us!(270_016));
const MP175_SEQ: [Step; 6] = mp_sequence(us!(340_025));

/// QSSTV MP73, 320x256, 16-bit VIS 0x2523.
pub const MP_73: Layout = pd_layout(320, 256, &[&MP73_SEQ]);
/// QSSTV MP115, 320x256, 16-bit VIS 0x2923.
pub const MP_115: Layout = pd_layout(320, 256, &[&MP115_SEQ]);
/// QSSTV MP140, 320x256, 16-bit VIS 0x2A23.
pub const MP_140: Layout = pd_layout(320, 256, &[&MP140_SEQ]);
/// QSSTV MP175, 320x256, 16-bit VIS 0x2C23.
pub const MP_175: Layout = pd_layout(320, 256, &[&MP175_SEQ]);

// QSSTV modeRobot2 (MR*/ML*): Y at 2x chroma, separators from `blank`,
// sync at the end of the line. MR175 shares VIS 0x4A23 with MR140 and is omitted.
const fn mr_sequence(y: Duration, chroma: Duration) -> [Step; 8] {
    [
        Step::scan(Channel::Y, y),
        Step::tone(Hz!(1500), us!(667)),
        Step::tone(Hz!(1900), us!(333)),
        Step::scan(Channel::RY, chroma),
        Step::tone(Hz!(2300), us!(667)),
        Step::tone(Hz!(1900), us!(333)),
        Step::scan(Channel::BY, chroma),
        Step::tone(Hz!(1200), ms!(9)),
    ]
}

const fn ml_porch_sequence(y: Duration, chroma: Duration) -> [Step; 10] {
    [
        Step::tone(Hz!(1500), us!(500)),
        Step::scan(Channel::Y, y),
        Step::tone(Hz!(1500), us!(333)),
        Step::tone(Hz!(1900), us!(167)),
        Step::scan(Channel::RY, chroma),
        Step::tone(Hz!(2300), us!(333)),
        Step::tone(Hz!(1900), us!(167)),
        Step::scan(Channel::BY, chroma),
        Step::tone(Hz!(1500), us!(500)),
        Step::tone(Hz!(1200), ms!(9)),
    ]
}

const fn yuv_layout(
    width: usize,
    height: usize,
    sequences: &'static [&'static [Step]],
) -> Layout {
    Layout {
        width,
        height,
        sequences,
        lines_per_sequence: 1,
        color: ColorMode::Yuv,
    }
}

const MR73_SEQ: [Step; 8] = mr_sequence(us!(137_660), us!(68_830));
const MR90_SEQ: [Step; 8] = mr_sequence(us!(170_662), us!(85_331));
const MR115_SEQ: [Step; 8] = mr_sequence(us!(219_666), us!(109_833));
const MR140_SEQ: [Step; 8] = mr_sequence(us!(268_668), us!(134_334));
const ML180_SEQ: [Step; 10] = ml_porch_sequence(us!(176_162), us!(88_081));
const ML240_SEQ: [Step; 10] = ml_porch_sequence(us!(236_166), us!(118_083));
const ML280_SEQ: [Step; 8] = mr_sequence(us!(277_167), us!(138_584));
const ML320_SEQ: [Step; 8] = mr_sequence(us!(317_172), us!(158_586));

/// QSSTV MR73, 320x256, 16-bit VIS 0x4523.
pub const MR_73: Layout = yuv_layout(320, 256, &[&MR73_SEQ]);
/// QSSTV MR90, 320x256, 16-bit VIS 0x4623.
pub const MR_90: Layout = yuv_layout(320, 256, &[&MR90_SEQ]);
/// QSSTV MR115, 320x256, 16-bit VIS 0x4923.
pub const MR_115: Layout = yuv_layout(320, 256, &[&MR115_SEQ]);
/// QSSTV MR140, 320x256, 16-bit VIS 0x4A23. MR175 uses the same VIS and is omitted.
pub const MR_140: Layout = yuv_layout(320, 256, &[&MR140_SEQ]);
/// QSSTV ML180, 640x496, 16-bit VIS 0x8523.
pub const ML_180: Layout = yuv_layout(640, 496, &[&ML180_SEQ]);
/// QSSTV ML240, 640x496, 16-bit VIS 0x8623.
pub const ML_240: Layout = yuv_layout(640, 496, &[&ML240_SEQ]);
/// QSSTV ML280, 640x496, 16-bit VIS 0x8923.
pub const ML_280: Layout = yuv_layout(640, 496, &[&ML280_SEQ]);
/// QSSTV ML320, 640x496, 16-bit VIS 0x8A23.
pub const ML_320: Layout = yuv_layout(640, 496, &[&ML320_SEQ]);
