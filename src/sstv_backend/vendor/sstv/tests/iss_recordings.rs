// Test helpers outside #[test] functions are not covered by the clippy.toml
// test allowances.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! Tests against real off-air ISS SSTV recordings, captured by KG4AKV (Space
//! Comms) and encoded on orbit with MMSSTV — the de-facto standard encoder.
//!
//! The recordings stay outside the git history; the first test run fetches
//! them (~130 MB) via `tests/scripts/fetch_iss_recordings.py`.

use sstv::{DecodedImage, Decoder, Demodulator, Encoder, Mode, Synthesizer};

const PD_120_PERIOD: f64 = 0.508_48;
const PD_180_PERIOD: f64 = 0.754_24;

struct Recording {
    path: &'static str,
    mode: Mode,
    /// The mode's line period in seconds.
    period: f64,
    /// Whether the recording's header survived reception well enough for the
    /// mode to be detected; the Cristoforetti one faded during the header and
    /// exercises the sync-lock path instead.
    detectable: bool,
}

const RECORDINGS: &[Recording] = &[
    Recording {
        path: "tests/assets/iss/pd180-gagarin-80.wav",
        mode: Mode::Pd180,
        period: PD_180_PERIOD,
        detectable: true,
    },
    Recording {
        path: "tests/assets/iss/pd180-apollo-soyuz.wav",
        mode: Mode::Pd180,
        period: PD_180_PERIOD,
        detectable: true,
    },
    Recording {
        path: "tests/assets/iss/pd180-ariss-qso-astros.wav",
        mode: Mode::Pd180,
        period: PD_180_PERIOD,
        detectable: true,
    },
    Recording {
        path: "tests/assets/iss/pd180-ariss-qso-cristoforetti.wav",
        mode: Mode::Pd180,
        period: PD_180_PERIOD,
        detectable: false,
    },
    Recording {
        path: "tests/assets/iss/pd180-mai75-suitsat.wav",
        mode: Mode::Pd180,
        period: PD_180_PERIOD,
        detectable: true,
    },
    Recording {
        path: "tests/assets/iss/pd120-ariss-20-year-1.wav",
        mode: Mode::Pd120,
        period: PD_120_PERIOD,
        detectable: true,
    },
    Recording {
        path: "tests/assets/iss/pd120-ariss-20-year-2.wav",
        mode: Mode::Pd120,
        period: PD_120_PERIOD,
        detectable: true,
    },
];

/// Fetch the recordings on the first use of a test run; both tests may ask
/// concurrently, so the download runs at most once.
fn ensure_recordings() {
    static FETCH: std::sync::Once = std::sync::Once::new();
    FETCH.call_once(|| {
        if RECORDINGS
            .iter()
            .all(|entry| std::path::Path::new(entry.path).exists())
        {
            return;
        }
        eprintln!("fetching the ISS recordings (~130 MB)");
        let status = std::process::Command::new("python3")
            .arg("tests/scripts/fetch_iss_recordings.py")
            .status()
            .expect("run tests/scripts/fetch_iss_recordings.py");
        assert!(status.success(), "fetching the ISS recordings failed");
    });
}

fn recording(path: &str) -> Vec<u8> {
    ensure_recordings();
    std::fs::read(path).unwrap_or_else(|_| panic!("{path} not found after fetching"))
}

/// Read a WAV's samples (first channel, scaled to 16 bit) and sample rate.
fn samples(wav: &[u8]) -> (Vec<i16>, u32) {
    let reader = hound::WavReader::new(std::io::Cursor::new(wav)).expect("parse wav");
    let spec = reader.spec();
    let channels = spec.channels as usize;
    let samples = match spec.sample_format {
        hound::SampleFormat::Int => reader
            .into_samples::<i32>()
            .map(|sample| sample.expect("read sample"))
            .step_by(channels)
            .map(|sample| (sample >> (spec.bits_per_sample.saturating_sub(16))) as i16)
            .collect(),
        hound::SampleFormat::Float => reader
            .into_samples::<f32>()
            .map(|sample| sample.expect("read sample"))
            .step_by(channels)
            .map(|sample| (sample * f32::from(i16::MAX)) as i16)
            .collect(),
    };
    (samples, spec.sample_rate)
}

fn decode(mode: Mode, wav: &[u8]) -> DecodedImage {
    Decoder::from_wav(mode, wav)
        .expect("parse wav")
        .images()
        .next()
        .expect("an image")
}

/// Mean absolute per-channel error between two images of equal length.
fn mean_abs_error(a: &DecodedImage, b: &DecodedImage) -> f64 {
    assert_eq!(a.pixels().len(), b.pixels().len());
    let total: u64 = a
        .pixels()
        .iter()
        .zip(b.pixels())
        .map(|(p, q)| {
            let d = |x: u8, y: u8| u64::from((i32::from(x) - i32::from(y)).unsigned_abs());
            d(p.red(), q.red()) + d(p.green(), q.green()) + d(p.blue(), q.blue())
        })
        .sum();
    total as f64 / (a.pixels().len() as f64 * 3.0)
}

/// The median spacing and length of the line sync pulses in a signal, in
/// seconds. Spacings are filtered to those near `expected_period` so that
/// header tones and reception glitches do not skew the median.
fn line_timing(samples: &[i16], sample_rate: u32, expected_period: f64) -> (f64, f64) {
    let mut starts: Vec<usize> = Vec::new();
    let mut lengths: Vec<usize> = Vec::new();
    let mut run = 0usize;
    // PD sync pulses are 20 ms; half of that separates them from glitches.
    let min_run = (f64::from(sample_rate) * 0.010) as usize;
    for (index, frequency) in Demodulator::new(samples.iter().copied(), sample_rate).enumerate() {
        if frequency.hz().abs_diff(1200) <= 150 {
            run += 1;
        } else {
            if run >= min_run {
                starts.push(index - run);
                lengths.push(run);
            }
            run = 0;
        }
    }

    let mut periods: Vec<f64> = starts
        .windows(2)
        .map(|pair| (pair[1] - pair[0]) as f64 / f64::from(sample_rate))
        .filter(|period| (period - expected_period).abs() < expected_period * 0.1)
        .collect();
    periods.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let mut lengths: Vec<f64> = lengths
        .into_iter()
        .map(|length| length as f64 / f64::from(sample_rate))
        .collect();
    lengths.sort_by(|a, b| a.partial_cmp(b).unwrap());

    assert!(periods.len() > 20, "too few line syncs found");
    (periods[periods.len() / 2], lengths[lengths.len() / 2])
}

/// The recordings decode completely — with the mode detected from the
/// transmitted VIS code where the header survived reception.
#[test]
fn decodes_the_recordings() {
    for entry in RECORDINGS {
        let wav = recording(entry.path);
        let decoder_mode = if entry.detectable {
            Mode::Auto
        } else {
            entry.mode
        };
        let image = decode(decoder_mode, &wav);
        assert_eq!(image.mode(), entry.mode, "{}", entry.path);
        assert!(image.complete(), "{} should decode completely", entry.path);
    }
}

/// Re-encoding the decoded image must reproduce the recorded transmission:
/// the synthesized tones match the recording's line timing, and decoding them
/// returns the same image.
#[test]
fn reencoding_matches_the_recorded_tones_and_images() {
    for entry in RECORDINGS {
        let (path, mode, period) = (entry.path, entry.mode, entry.period);
        let wav = recording(path);
        let (recorded_samples, sample_rate) = samples(&wav);
        let recorded_image = decode(mode, &wav);

        let encoder = Encoder::new(mode, recorded_image.pixels().to_vec().into_iter())
            .expect("construct encoder");
        let synthesized: Vec<i16> = Synthesizer::new(encoder, sample_rate).collect();

        // The tones: line sync pulses must be spaced and sized like the
        // recording's (which MMSSTV derived from the same paper timings).
        let (recorded_period, recorded_sync) = line_timing(&recorded_samples, sample_rate, period);
        let (our_period, our_sync) = line_timing(&synthesized, sample_rate, period);
        let period_error = (recorded_period - our_period).abs() / recorded_period;
        assert!(
            period_error < 0.003,
            "{path}: line period differs by {:.3}% ({recorded_period}s vs {our_period}s)",
            period_error * 100.0
        );
        let sync_error = (recorded_sync - our_sync).abs() / recorded_sync;
        assert!(
            sync_error < 0.15,
            "{path}: sync length differs by {:.1}% ({recorded_sync}s vs {our_sync}s)",
            sync_error * 100.0
        );

        // The images: scan frequencies map linearly to pixel values, so this
        // bounds the mean tone error (one pixel step is ~3.1 Hz).
        let mut wav_out = std::io::Cursor::new(Vec::new());
        let spec = hound::WavSpec {
            channels: 1,
            sample_rate,
            bits_per_sample: 16,
            sample_format: hound::SampleFormat::Int,
        };
        let mut writer = hound::WavWriter::new(&mut wav_out, spec).expect("write wav");
        for sample in &synthesized {
            writer.write_sample(*sample).expect("write sample");
        }
        writer.finalize().expect("finalize wav");
        let reencoded_image = decode(mode, wav_out.get_ref());

        let error = mean_abs_error(&recorded_image, &reencoded_image);
        assert!(error < 10.0, "{path}: mean abs error {error} too high");
    }
}
