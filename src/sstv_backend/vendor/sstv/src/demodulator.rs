use crate::units::Frequency;

/// Estimates the instantaneous frequency of a stream of PCM samples.
///
/// `Demodulator` is the inverse of [`Synthesizer`](crate::Synthesizer): it turns
/// 16-bit samples back into a stream of [`Frequency`] estimates. It performs no
/// protocol interpretation — grouping the frequency track into pixels and
/// scanlines is the decoder's job.
///
/// This is a deliberately simple *midline-crossing* estimator. It measures the
/// number of samples between every *other* crossing of the waveform's midline —
/// a full period — and holds that estimate until the next crossing is seen.
/// Measuring a full period rather than a single half-period cancels the
/// alternating long/short bias that appears when the midline is not perfectly
/// centred, which would otherwise make the estimate swing above and below the
/// true frequency every half cycle. The midline is the
/// midpoint of a running minimum and maximum that continuously relax toward it,
/// forming an adaptive envelope: the midline tracks a changing DC offset or
/// signal level, and an early transient (a click, a burst of static) cannot
/// latch it away from the signal — which would otherwise stop the crossings the
/// estimator depends on. The crossing position is linearly interpolated between
/// the two straddling samples for sub-sample accuracy. The approach is cheap and
/// allocation-free, but only moderately noise resistant and less accurate when
/// there are few samples per cycle.
///
/// The iterator yields *nothing* during an initial warm-up rather than a
/// placeholder value: it needs a pair of crossings to measure a period, and it
/// drops the first few crossings while the envelope grows to span a full cycle
/// and its midline is still biased. Once the first estimate is available it
/// yields one estimate per input sample. The dropped warm-up is bounded to a
/// few periods and, for SSTV, always falls inside the leader tone.
///
/// ```rust
/// use sstv::{Demodulator, Synthesizer, Tone, Hz, ms};
///
/// let samples = Synthesizer::new([Tone::new(Hz!(1900), ms!(20))].into_iter(), 48000);
/// for frequency in Demodulator::new(samples, 48000) {
///     // inspect the recovered frequency track
///     let _ = frequency;
/// }
/// ```
pub struct Demodulator<I: Iterator<Item = i16>> {
    samples: I,
    sample_rate: u32,
    previous_sample: i16,
    minimum: f64,
    maximum: f64,
    /// Per-sample factor by which the envelope relaxes toward the midline.
    envelope_decay: f64,
    index: u64,
    last_crossing: Option<f64>,
    earlier_crossing: Option<f64>,
    crossings_seen: u32,
    frequency: Option<Frequency>,
}

impl<I: Iterator<Item = i16>> Demodulator<I> {
    /// Crossings dropped before the first estimate, while the envelope grows to
    /// span a full cycle and its midline is still biased.
    const WARM_UP_CROSSINGS: u32 = 4;

    /// Create a new `Demodulator` from a sample iterator and a sample rate in Hz.
    ///
    /// `sample_rate` must be greater than zero.
    pub fn new(mut samples: I, sample_rate: u32) -> Self {
        let sample_rate = sample_rate.max(1);
        let first_sample = samples.next().unwrap_or_default();

        // Relax the envelope toward the midline with roughly a 100 ms time
        // constant: slow enough that it barely moves within one cycle (so the
        // midline stays put and does not manufacture crossings, and noise near
        // the peaks does not jitter it), fast enough to follow DC drift and
        // level changes and to recover from a transient well within the SSTV
        // header (~900 ms before the first line sync).
        let time_constant = (f64::from(sample_rate) * 0.1).max(1.0);
        let envelope_decay = libm::exp(-1.0 / time_constant);

        Self {
            samples,
            sample_rate,
            previous_sample: first_sample,
            minimum: f64::from(first_sample),
            maximum: f64::from(first_sample),
            envelope_decay,
            index: 0,
            last_crossing: None,
            earlier_crossing: None,
            crossings_seen: 0,
            frequency: None,
        }
    }

    /// The sample rate the demodulator was constructed with, in Hz.
    pub const fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    fn calculate_frequency(&mut self, current_sample: i16, index: u64) -> Option<Frequency> {
        // Relax the running extremes toward the midline, then re-expand to
        // include the new sample. This adaptive envelope keeps the midline
        // centred on the *current* waveform, so a DC offset, a level change, or
        // an early transient cannot latch it away from the signal.
        let sample = f64::from(current_sample);
        let midline = f64::midpoint(self.minimum, self.maximum);
        self.maximum = midline + (self.maximum - midline) * self.envelope_decay;
        self.minimum = midline + (self.minimum - midline) * self.envelope_decay;
        self.maximum = self.maximum.max(sample);
        self.minimum = self.minimum.min(sample);
        let midline = f64::midpoint(self.minimum, self.maximum);

        let previous_sample = self.previous_sample;
        self.previous_sample = current_sample;

        let previous_offset = f64::from(previous_sample) - midline;
        let current_offset = sample - midline;
        let samples_crossed_the_midline = (previous_offset >= 0.0) != (current_offset >= 0.0);
        if !samples_crossed_the_midline {
            return None;
        }

        let midline_crossing =
            (index as f64 - 1.0) + previous_offset / (previous_offset - current_offset);

        // Keep the last two crossings so we can measure a full period (from the
        // crossing two ago to this one). Summing a long and a short half-period
        // cancels the alternating bias of an off-centre midline.
        let earlier_crossing = self.earlier_crossing;
        self.earlier_crossing = self.last_crossing;
        self.last_crossing = Some(midline_crossing);

        // Drop the first few crossings: until the envelope has spanned a full
        // cycle its midline is still biased, so those periods are mistimed.
        self.crossings_seen = self.crossings_seen.saturating_add(1);
        if self.crossings_seen <= Self::WARM_UP_CROSSINGS {
            return None;
        }

        let period = midline_crossing - earlier_crossing?;
        let frequency_float = f64::from(self.sample_rate) / period;

        Some(Frequency::from_hz(frequency_float as u32))
    }
}

impl<I: Iterator<Item = i16>> Iterator for Demodulator<I> {
    type Item = Frequency;

    fn next(&mut self) -> Option<Frequency> {
        while self.frequency.is_none() {
            let current_sample = self.samples.next()?;
            let index = self.index;
            self.index += 1;

            self.frequency = self.calculate_frequency(current_sample, index);
        }

        let current_sample = self.samples.next()?;
        let index = self.index;
        self.index += 1;

        match self.calculate_frequency(current_sample, index) {
            Some(frequency) => {
                self.frequency = Some(frequency);
                Some(frequency)
            }
            None => self.frequency,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::synthesizer::{Synthesizer, Tone};
    use crate::units::Duration;
    use rand::SeedableRng;
    use rand_distr::{Distribution, Normal};

    #[test]
    fn pure_1500hz_at_48000() {
        let actual_frequency = Frequency::from_hz(1500);
        let sample_rate: u32 = 48_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn pure_2300hz_at_48000() {
        let actual_frequency = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn pure_1200hz_at_8000() {
        let actual_frequency = Frequency::from_hz(1200);
        let sample_rate: u32 = 8_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn pure_1000hz_at_8000() {
        let actual_frequency = Frequency::from_hz(1000);
        let sample_rate: u32 = 8_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn dc_offset_2300hz_at_48000() {
        let actual_frequency = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;

        // Synthesize at half scale to leave headroom, then shift the whole
        // waveform up so it is strictly positive and never touches zero.
        let samples = synthesize(vec![actual_frequency], sample_rate);
        let dc_offsets = vec![-samples.iter().min().unwrap() + 1; samples.len()];

        test_frequency(actual_frequency, sample_rate, &samples, dc_offsets);
    }

    #[test]
    fn noisy_2300hz_at_48000() {
        let actual_frequency = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;
        let signal_to_noise_ratio_db = 25.0;

        let samples = synthesize(vec![actual_frequency], sample_rate);
        let offsets = noise(samples.len(), signal_to_noise_ratio_db);
        test_frequency(actual_frequency, sample_rate, &samples, offsets);
    }

    #[test]
    fn switch_between_two_frequencies() {
        let first_actual = Frequency::from_hz(1500);
        let second_actual = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;

        let samples = synthesize(vec![first_actual, second_actual], sample_rate);
        let estimates: Vec<Frequency> =
            Demodulator::new(samples.into_iter(), sample_rate).collect();

        // Every estimate should fall within the band spanned by the two tones
        // (plus tolerance). At the switch a single full-period measurement
        // straddles both frequencies and lands between them, which is fine; a
        // wild excursion outside the band is not.
        let low = first_actual.hz() - first_actual.hz() / 20;
        let high = second_actual.hz() + second_actual.hz() / 20;
        for estimated in estimates {
            assert!(
                (low..=high).contains(&estimated.hz()),
                "{} Hz outside [{}, {}]",
                estimated.hz(),
                low,
                high,
            );
        }
    }

    #[test]
    fn empty_samples_yields_empty_frequencies() {
        let mut estimates = Demodulator::new(core::iter::empty(), 48_000);

        assert!(estimates.next().is_none());
    }

    fn synthesize(frequencies: Vec<Frequency>, sample_rate: u32) -> Vec<i16> {
        let tones = frequencies
            .into_iter()
            .map(|freq| Tone::new(freq, Duration::from_ms(10)));
        Synthesizer::new(tones, sample_rate)
            .map(|sample| sample / 2)
            .collect()
    }

    fn test_frequency(
        actual_frequency: Frequency,
        sample_rate: u32,
        samples: &[i16],
        offsets: Vec<i16>,
    ) {
        let samples_with_offset = samples
            .iter()
            .zip(offsets.into_iter().chain(core::iter::repeat(0)))
            .map(|(s, o)| s.saturating_add(o));

        let estimates: Vec<Frequency> =
            Demodulator::new(samples_with_offset, sample_rate).collect();

        assert!(!estimates.is_empty());

        for estimate in estimates {
            assert!(
                frequencies_match(estimate, actual_frequency),
                "median {} Hz ≉ {} Hz",
                estimate.hz(),
                actual_frequency.hz(),
            );
        }
    }

    fn frequencies_match(estimated: Frequency, actual: Frequency) -> bool {
        let allowed_deviation = actual / 20;
        let deviation = estimated.abs_diff(actual);
        deviation <= allowed_deviation
    }

    fn noise(len: usize, snr_db: f64) -> Vec<i16> {
        let signal_rms = f64::from(i16::MAX) / core::f64::consts::SQRT_2 / 2.0;
        let sigma = signal_rms / 10f64.powf(snr_db / 20.0);

        let mut rng = rand::rngs::StdRng::seed_from_u64(0x5EED);
        let normal = Normal::new(0.0, sigma).unwrap();
        (0..len)
            .map(|_| {
                normal
                    .sample(&mut rng)
                    .round()
                    .clamp(f64::from(i16::MIN), f64::from(i16::MAX)) as i16
            })
            .collect()
    }
}
