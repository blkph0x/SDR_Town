//! Locates the start of a transmission within the frequency stream: by its
//! calibration header's VIS code when detecting the mode, or by the spacing
//! of its line sync pulses when the mode is known.

use crate::Frequency;
use crate::modes::layout::Layout;
use crate::modes::{LEADER_FREQUENCY, Mode, SYNC_FREQUENCY};

use super::stream::FrequencyStream;

/// How far a frequency may stray from a nominal tone and still count as it.
const TONE_TOLERANCE_HZ: u32 = 150;

pub(super) const fn is_sync(frequency: Frequency) -> bool {
    frequency.hz().abs_diff(SYNC_FREQUENCY.hz()) <= TONE_TOLERANCE_HZ
}

const fn is_leader(frequency: Frequency) -> bool {
    frequency.hz().abs_diff(LEADER_FREQUENCY.hz()) <= TONE_TOLERANCE_HZ
}

/// Buffer frequencies until a line sync is located, and return the sample
/// position at which that line's timing sequence begins.
///
/// A candidate sync is a run near the sync frequency lasting roughly one
/// sync-pulse duration (long enough to exclude glitches, short enough to
/// exclude the header's longer tones). An over-long run's tail is also a
/// candidate: the VIS stop bit runs directly into the first line's sync
/// pulse, merging both into one run. Three candidates evenly spaced one
/// sequence period apart — the later two being clean, properly sized runs —
/// are the line syncs of consecutive lines; noise does not produce that
/// pattern. The image begins one sync offset before the first of them (zero
/// for most modes — Scottie places the sync pulse mid-sequence).
pub(super) fn detect_by_sync_period<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
) -> Option<(Mode, f64)> {
    let mut best: Option<(Mode, f64, f64)> = None;
    for mode in Mode::ALL {
        if mode.layout().sync_pulse().1.ns() == 0 {
            continue;
        }
        if let Some((start, error)) = lock_onto_first_line_scored(stream, &mode.layout()) {
            let better = match best {
                None => true,
                Some((_, _, previous)) => error + 1e-12 < previous,
            };
            if better {
                best = Some((mode, start, error));
            }
        }
    }
    best.map(|(mode, start, _)| (mode, start))
}

pub(super) fn lock_onto_first_line<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
    layout: &Layout,
) -> Option<f64> {
    lock_onto_first_line_scored(stream, layout).map(|(start, _)| start)
}

fn lock_onto_first_line_scored<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
    layout: &Layout,
) -> Option<(f64, f64)> {
    let (sync_offset, sync_duration) = layout.sync_pulse();
    if sync_duration.ns() == 0 {
        return None;
    }
    let sync_len = stream.samples_in(sync_duration);
    let period = stream.samples_in(layout.sequence_duration());

    let min_run = (sync_len * 0.5) as usize;
    let max_run = (sync_len * 2.0) as usize;
    let spacing_tolerance = period * 0.15;
    let spaced = |from: usize, to: usize| ((to - from) as f64 - period).abs() <= spacing_tolerance;

    // Recent candidates: (position, whether the run was clean).
    let mut candidates: alloc::vec::Vec<(usize, bool)> = alloc::vec::Vec::new();
    let mut index = 0usize;
    loop {
        if !is_sync(stream.peek(index)?) {
            index += 1;
            continue;
        }

        let start = index;
        while is_sync(stream.peek(index)?) {
            index += 1;
        }
        let run = index - start;
        let (position, clean) = if (min_run..=max_run).contains(&run) {
            (start, true)
        } else if run > max_run {
            // Assume the tail of the over-long run is a merged sync pulse.
            (index - sync_len as usize, false)
        } else {
            continue;
        };

        // A clean candidate can complete a triple (a, b, this).
        if clean {
            for &(b, b_clean) in candidates.iter().rev() {
                if !b_clean || !spaced(b, position) {
                    continue;
                }
                for &(a, _) in candidates.iter().rev() {
                    if a < b && spaced(a, b) {
                        let first = (b - a) as f64;
                        let second = (position - b) as f64;
                        let error = ((first - period).abs() + (second - period).abs())
                            / (2.0 * period);
                        return Some((a as f64 - stream.samples_in(sync_offset), error));
                    }
                }
            }
        }

        candidates.push((position, clean));
        // Only candidates within two periods (plus slack) can still form a
        // triple with future syncs.
        candidates.retain(|&(p, _)| (index - p) as f64 <= period * 2.5);
    }
}

/// Buffer frequencies until a calibration header is found, returning the
/// detected mode and the sample position at which its first timing sequence
/// begins.
///
/// The header is located by its break: a short burst of the sync frequency
/// splitting the two leader tones. From there the VIS bits lie at fixed
/// offsets and are sampled near their centres; the mode is accepted once the
/// start, stop and parity bits check out and the code is known.
pub(super) fn detect_mode<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
) -> Option<(Mode, f64)> {
    let sample_rate = f64::from(stream.sample_rate());
    let samples = move |milliseconds: f64| milliseconds / 1000.0 * sample_rate;
    let min_leader = samples(150.0) as usize;
    let min_break = samples(4.0) as usize;
    let max_break = samples(25.0) as usize;
    let max_gap = samples(3.0) as usize;

    // The most recently completed run of leader-tone frequencies, as
    // (end, length); a break qualifies only right after a long one.
    let mut leader: Option<(usize, usize)> = None;
    let mut leader_start: Option<usize> = None;
    let mut break_start: Option<usize> = None;
    let mut index = 0usize;
    loop {
        let frequency = stream.peek(index)?;

        if is_leader(frequency) {
            leader_start.get_or_insert(index);
        } else if let Some(start) = leader_start.take() {
            let length = index - start;
            leader = Some((index, length));
            // A recording trimmed at the front loses the leader-break-leader
            // preamble, leaving a lone leader running straight into the VIS
            // start bit. Anchor on that transition: when a long-enough leader
            // ends, try reading the VIS as if its bits begin right there. On a
            // full header this reads the break tone as the start bit and fails
            // the bit checks, leaving the break-anchored path below to detect.
            if length >= min_leader
                && let Some(found) = read_vis_bits(stream, index)
            {
                return Some(found);
            }
        }

        if is_sync(frequency) {
            break_start.get_or_insert(index);
        } else if let Some(start) = break_start.take() {
            let run = index - start;
            let after_leader = matches!(
                leader,
                Some((end, length))
                    if start.saturating_sub(end) <= max_gap && length >= min_leader
            );
            if after_leader
                && (min_break..=max_break).contains(&run)
                && let Some(found) = read_vis(stream, index)
            {
                return Some(found);
            }
        }

        index += 1;
    }
}

/// Read the VIS code whose break ended at `break_end`, returning the mode and
/// the position where its image data begins. `None` if anything about the
/// bits is off — the search then simply continues.
fn read_vis<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
    break_end: usize,
) -> Option<(Mode, f64)> {
    let sample_rate = f64::from(stream.sample_rate());
    let samples = move |milliseconds: f64| milliseconds / 1000.0 * sample_rate;

    // The second leader tone fills the 300ms between break and start bit.
    for probe in [100.0, 200.0] {
        if !is_leader(stream.peek(break_end + samples(probe) as usize)?) {
            return None;
        }
    }

    read_vis_bits(stream, break_end + samples(300.0) as usize)
}

/// Read the ten VIS bits beginning at `start_bit`, returning the mode and the
/// position where its image data begins. `None` if anything about the bits is
/// off — the search then simply continues.
fn read_vis_bits<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
    start_bit: usize,
) -> Option<(Mode, f64)> {
    let sample_rate = f64::from(stream.sample_rate());
    let samples = move |milliseconds: f64| milliseconds / 1000.0 * sample_rate;

    // Ten 30ms bits: start, seven code bits (least-significant first), parity
    // and stop. Each is judged by the median of three samples around its
    // centre.
    let mut bits = [0u32; 10];
    for (slot, bit) in bits.iter_mut().enumerate() {
        let mut medians = [0u32; 3];
        for (sample, offset) in medians.iter_mut().zip([8.0, 15.0, 22.0]) {
            let position = start_bit + samples(slot as f64 * 30.0 + offset) as usize;
            *sample = stream.peek(position)?.hz();
        }
        medians.sort_unstable();
        *bit = medians[1];
    }

    let is_sync_bit = |hz: u32| hz.abs_diff(SYNC_FREQUENCY.hz()) <= TONE_TOLERANCE_HZ;
    if is_sync_bit(bits[0]) && is_sync_bit(bits[9]) {
        let mut code = 0u8;
        let mut ones = 0u32;
        let mut tones_ok = true;
        for (bit, hz) in bits[1..=8].iter().enumerate() {
            if !(950..=1450).contains(hz) {
                tones_ok = false;
                break;
            }
            if *hz < 1200 {
                ones += 1;
                if bit < 7 {
                    code |= 1 << bit;
                }
            }
        }
        if tones_ok && ones.is_multiple_of(2)
            && let Some(mode) = Mode::from_vis_code(code)
        {
            // The ten bits span 300ms; image data follows the stop bit.
            let mut sequence_start = start_bit as f64 + samples(300.0);
            if mode.has_starting_sync_pulse() {
                sequence_start += stream.samples_in(mode.layout().sync_pulse().1);
            }
            return Some((mode, sequence_start));
        }
    }
    read_vis_word(stream, start_bit)
}

/// QSSTV 16-bit VIS: start + 16 data bits + stop (low byte 0x23).
fn read_vis_word<I: Iterator<Item = i16>>(
    stream: &mut FrequencyStream<I>,
    start_bit: usize,
) -> Option<(Mode, f64)> {
    let sample_rate = f64::from(stream.sample_rate());
    let samples = move |milliseconds: f64| milliseconds / 1000.0 * sample_rate;

    let mut bits = [0u32; 18];
    for (slot, bit) in bits.iter_mut().enumerate() {
        let mut medians = [0u32; 3];
        for (sample, offset) in medians.iter_mut().zip([8.0, 15.0, 22.0]) {
            let position = start_bit + samples(slot as f64 * 30.0 + offset) as usize;
            *sample = stream.peek(position)?.hz();
        }
        medians.sort_unstable();
        *bit = medians[1];
    }

    let is_sync_bit = |hz: u32| hz.abs_diff(SYNC_FREQUENCY.hz()) <= TONE_TOLERANCE_HZ;
    if !is_sync_bit(bits[0]) || !is_sync_bit(bits[17]) {
        return None;
    }
    let mut word = 0u16;
    for (bit, hz) in bits[1..=16].iter().enumerate() {
        if !(950..=1450).contains(hz) {
            return None;
        }
        if *hz < 1200 {
            word |= 1 << bit;
        }
    }
    let mode = Mode::from_vis_word(word)?;
    let mut sequence_start = start_bit as f64 + samples(540.0);
    if mode.has_starting_sync_pulse() {
        sequence_start += stream.samples_in(mode.layout().sync_pulse().1);
    }
    Some((mode, sequence_start))
}
