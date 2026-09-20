//! A demodulated frequency stream with lookahead and replay: acquisition must
//! scan far ahead without losing the signal it inspects, so frequencies are
//! buffered while peeking and consumed again once decoding starts.

use alloc::vec::Vec;

use crate::units::Duration;
use crate::{Demodulator, Frequency};

pub(super) struct FrequencyStream<I: Iterator<Item = i16>> {
    demodulator: Demodulator<I>,
    /// Frequencies buffered by [`peek`](Self::peek), replayed by
    /// [`advance`](Self::advance) before live samples are pulled.
    buffer: Vec<Frequency>,
    /// Read position within `buffer` while replaying.
    replay: usize,
    /// Samples consumed since the origin was last moved.
    position: usize,
    current: Frequency,
}

impl<I: Iterator<Item = i16>> FrequencyStream<I> {
    pub const fn new(demodulator: Demodulator<I>) -> Self {
        Self {
            demodulator,
            buffer: Vec::new(),
            replay: 0,
            position: 0,
            current: Frequency::from_hz(0),
        }
    }

    pub const fn sample_rate(&self) -> u32 {
        self.demodulator.sample_rate()
    }

    /// The duration expressed in (fractional) samples.
    pub fn samples_in(&self, duration: Duration) -> f64 {
        duration_to_samples(duration, self.sample_rate())
    }

    /// Samples consumed since the origin was last moved.
    pub const fn position(&self) -> usize {
        self.position
    }

    /// The most recently consumed frequency.
    pub const fn current(&self) -> Frequency {
        self.current
    }

    /// The frequency `index` samples past the origin, buffering as needed
    /// without consuming anything. `None` once the stream is exhausted.
    pub fn peek(&mut self, index: usize) -> Option<Frequency> {
        while self.buffer.len() <= index {
            self.buffer.push(self.demodulator.next()?);
        }
        Some(self.buffer[index])
    }

    /// Discard the lookahead buffer and make the current point the origin.
    pub fn reset(&mut self) {
        self.buffer.clear();
        self.replay = 0;
        self.position = 0;
    }

    /// Move the origin `origin` samples forward (clamped to the buffered
    /// lookahead) and restart consumption there. Returns the amount actually
    /// moved.
    pub fn start_at(&mut self, origin: usize) -> usize {
        let origin = origin.min(self.buffer.len());
        self.buffer.drain(..origin);
        self.replay = 0;
        self.position = 0;
        origin
    }

    /// Consume one sample.
    pub fn advance(&mut self) -> Option<()> {
        self.current = if self.replay < self.buffer.len() {
            let frequency = self.buffer[self.replay];
            self.replay += 1;
            frequency
        } else {
            self.demodulator.next()?
        };
        self.position += 1;
        Some(())
    }

    /// Consume samples up to the given (rounded) position and return the
    /// frequency there.
    pub fn advance_to(&mut self, position: f64) -> Option<Frequency> {
        let target = libm::round(position.max(0.0)) as usize;
        while self.position < target {
            self.advance()?;
        }
        Some(self.current)
    }
}

pub(super) fn duration_to_samples(duration: Duration, sample_rate: u32) -> f64 {
    duration.ns() as f64 * f64::from(sample_rate) / 1_000_000_000.0
}
