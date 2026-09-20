mod acquire;
mod assemble;
mod stream;

use alloc::collections::VecDeque;
use alloc::{vec, vec::Vec};

use crate::modes::layout::{Layout, Step};
use crate::modes::{BLACK_FREQUENCY, Mode, SYNC_FREQUENCY, WHITE_FREQUENCY};
use crate::{Demodulator, RgbPixel};

use acquire::{detect_by_sync_period, detect_mode, is_sync, lock_onto_first_line};
use assemble::{Assembler, SequenceData};
use stream::FrequencyStream;

/// A single decoded scanline: one image width of pixels in left-to-right order.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RgbRow {
    index: usize,
    pixels: Vec<RgbPixel>,
}

impl RgbRow {
    /// The row's vertical position within its image; `0` is the top row.
    #[must_use]
    pub const fn index(&self) -> usize {
        self.index
    }

    /// The row's pixels, left to right.
    #[must_use]
    pub fn pixels(&self) -> &[RgbPixel] {
        &self.pixels
    }
}

/// An event produced by a [`Decoder`] while scanning a sample stream.
///
/// A single image is reported as an [`ImageStart`](Event::ImageStart), followed
/// by one [`Row`](Event::Row) per decoded scanline in top-to-bottom order,
/// followed by an [`ImageEnd`](Event::ImageEnd). A stream that contains several
/// images — with or without gaps between them — yields these groups back to
/// back, one per image.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Event {
    /// A new image in the given mode has been acquired; its rows follow. The
    /// image dimensions are the mode's [`image_width`](Mode::image_width) and
    /// [`image_height`](Mode::image_height).
    ImageStart(Mode),
    /// One decoded scanline of the current image.
    Row(RgbRow),
    /// The current image finished.
    ImageEnd {
        /// Whether every scanline was decoded. `false` if the image was
        /// truncated before completion (for example, the signal faded out).
        complete: bool,
    },
}

/// Decodes SSTV transmissions from an audio sample stream.
///
/// `Decoder` is the streaming inverse of [`Encoder`](crate::Encoder). It runs
/// a [`Demodulator`] over 16-bit PCM samples and walks the mode's timing
/// sequence as specified in the Dayton paper, re-aligning on every sync
/// pulse. Acquisition happens lazily as the output is polled: a stream that
/// never contains an image simply yields nothing, and a stream carrying
/// several images — with or without gaps between them — yields all of them.
///
/// Construct it from a sample stream ([`from_samples`](Self::from_samples))
/// or an existing demodulator ([`from_demodulator`](Self::from_demodulator)),
/// then choose how to consume it: [`events`](Self::events) streams scanlines
/// as they are recovered, holding only about one line group in memory, while
/// [`images`](Self::images) assembles and yields whole images.
///
/// ```no_run
/// use sstv::{Decoder, Mode};
///
/// # let samples = std::vec::Vec::<i16>::new().into_iter();
/// for image in Decoder::from_samples(Mode::Auto, samples, 48000).images() {
///     let _ = (image.mode(), image.pixels());
/// }
/// ```
pub struct Decoder<I: Iterator<Item = i16>> {
    events: Events<I>,
}

impl<I: Iterator<Item = i16>> Decoder<I> {
    /// Decode a stream of PCM samples.
    ///
    /// With [`Mode::Auto`], each image's mode is detected from its header's
    /// VIS code. `sample_rate` must be greater than zero. Construction never
    /// fails: finding images is deferred to iteration.
    pub fn from_samples(mode: Mode, samples: I, sample_rate: u32) -> Self {
        let sample_rate = sample_rate.max(1);
        Self::from_demodulator(mode, Demodulator::new(samples, sample_rate))
    }

    /// Decode the frequency stream of an existing demodulator.
    pub const fn from_demodulator(mode: Mode, demodulator: Demodulator<I>) -> Self {
        Self {
            events: Events::new(mode, demodulator),
        }
    }

    /// Assume the samples begin directly at the image data and skip searching
    /// for a header.
    ///
    /// Decoding starts immediately at the first line's timing sequence. Use
    /// this when the signal carries no detectable header, or when acquisition
    /// has already been performed upstream. After the first image completes,
    /// the decoder searches for further images as usual. [`Mode::Auto`]
    /// cannot be detected without a header and decodes as [`Mode::Robot36`].
    #[must_use]
    pub fn without_header(mut self) -> Self {
        self.events.skip_header();
        self
    }

    /// Stream scanlines as they are recovered, grouped into images by
    /// [`Event::ImageStart`] and [`Event::ImageEnd`] markers.
    pub fn events(self) -> Events<I> {
        self.events
    }

    /// Assemble and stream whole images, one at a time.
    pub fn images(self) -> Images<I> {
        Images {
            events: self.events,
        }
    }
}

#[cfg(feature = "wav")]
impl Decoder<alloc::vec::IntoIter<i16>> {
    /// Decode a transmission from in-memory WAV data.
    ///
    /// Only the first channel of multi-channel audio is used; integer samples
    /// of any bit depth and float samples are converted to 16 bit.
    ///
    /// Data cut short relative to the length declared in the header — common
    /// in recordings whose writer was interrupted — is decoded up to the cut;
    /// pixels the audio did not carry are left black.
    ///
    /// # Errors
    ///
    /// Fails if the WAV data is malformed.
    pub fn from_wav(mode: Mode, wav: &[u8]) -> core::result::Result<Self, hound::Error> {
        // `hound` reports running out of data mid-sample as an I/O error;
        // treat that as end of stream to tolerate truncated recordings.
        fn or_eof<T>(
            sample: core::result::Result<T, hound::Error>,
        ) -> core::result::Result<Option<T>, hound::Error> {
            match sample {
                Ok(sample) => Ok(Some(sample)),
                Err(hound::Error::IoError(_)) => Ok(None),
                Err(error) => Err(error),
            }
        }

        let reader = hound::WavReader::new(std::io::Cursor::new(wav))?;
        let spec = reader.spec();
        let channels = spec.channels.max(1) as usize;

        let mut samples = Vec::with_capacity(reader.len() as usize / channels);
        match spec.sample_format {
            hound::SampleFormat::Int => {
                for (index, sample) in reader.into_samples::<i32>().enumerate() {
                    let Some(sample) = or_eof(sample)? else { break };
                    if index % channels == 0 {
                        let scaled = if spec.bits_per_sample >= 16 {
                            sample >> (spec.bits_per_sample - 16)
                        } else {
                            sample << (16 - spec.bits_per_sample)
                        };
                        samples.push(scaled as i16);
                    }
                }
            }
            hound::SampleFormat::Float => {
                for (index, sample) in reader.into_samples::<f32>().enumerate() {
                    let Some(sample) = or_eof(sample)? else { break };
                    if index % channels == 0 {
                        samples.push((sample * f32::from(i16::MAX)) as i16);
                    }
                }
            }
        }

        Ok(Self::from_samples(
            mode,
            samples.into_iter(),
            spec.sample_rate,
        ))
    }
}

#[cfg(feature = "mp3")]
impl Decoder<alloc::vec::IntoIter<i16>> {
    /// Decode a transmission from in-memory MP3 data.
    ///
    /// Only the first channel of multi-channel audio is used.
    ///
    /// # Errors
    ///
    /// Fails if the MP3 data is malformed.
    pub fn from_mp3(mode: Mode, mp3: &[u8]) -> core::result::Result<Self, minimp3::Error> {
        let mut frames = minimp3::Decoder::new(std::io::Cursor::new(mp3));
        let mut samples = Vec::new();
        let mut sample_rate = 0u32;
        loop {
            match frames.next_frame() {
                Ok(frame) => {
                    sample_rate = frame.sample_rate as u32;
                    let channels = frame.channels.max(1);
                    samples.extend(frame.data.iter().step_by(channels));
                }
                Err(minimp3::Error::Eof) => break,
                Err(error) => return Err(error),
            }
        }

        Ok(Self::from_samples(mode, samples.into_iter(), sample_rate))
    }
}

#[cfg(feature = "image")]
impl<I: Iterator<Item = i16>> Decoder<I> {
    /// Assemble and stream whole images as `image` crate buffers, ready for
    /// its processing and saving APIs.
    ///
    /// This drops the mode and completeness metadata; use
    /// [`images`](Self::images) to keep it.
    ///
    /// ```no_run
    /// use sstv::{Decoder, Mode};
    ///
    /// # let samples = std::vec::Vec::<i16>::new().into_iter();
    /// for (index, image) in Decoder::from_samples(Mode::Auto, samples, 48000)
    ///     .rgb_images()
    ///     .enumerate()
    /// {
    ///     image.save(format!("{index}.png")).expect("save image");
    /// }
    /// ```
    pub fn rgb_images(self) -> impl Iterator<Item = image::RgbImage> {
        self.images().map(|decoded| image::RgbImage::from(&decoded))
    }
}

/// The event stream of a [`Decoder`]: scanlines as they are recovered,
/// grouped into images by [`Event::ImageStart`] and [`Event::ImageEnd`]
/// markers.
///
/// It holds at most about one line group at a time — never the whole
/// frequency track or image.
pub struct Events<I: Iterator<Item = i16>> {
    stream: FrequencyStream<I>,
    /// The mode requested at construction, possibly [`Mode::Auto`].
    requested_mode: Mode,
    state: State,
    /// Decoded events waiting to be handed out, oldest first.
    queue: VecDeque<Event>,
}

/// Where the decoder is in the acquire → decode → acquire cycle.
enum State {
    /// Scanning for the next image.
    Searching,
    /// Decoding the rows of the current image.
    Decoding(ImageState),
    /// The sample stream is exhausted; no more events will be produced.
    Done,
}

/// Everything needed to decode the image currently being worked on.
struct ImageState {
    mode: Mode,
    layout: Layout,
    /// Fractional sample position at which the next timing sequence begins.
    sequence_start: f64,
    /// Which of the mode's timing sequences the next line uses.
    sequence_index: usize,
    /// Index of the next image line to decode.
    row_index: usize,
    assembler: Assembler,
}

impl ImageState {
    fn new(mode: Mode, sequence_start: f64) -> Self {
        let layout = mode.layout();
        Self {
            mode,
            layout,
            sequence_start,
            sequence_index: 0,
            row_index: 0,
            assembler: Assembler::new(&layout),
        }
    }

    /// Walk one timing sequence: consume sync pulses to re-align, sample the
    /// centre of every other tone, and sample each scan's pixels.
    ///
    /// A transmission ends flush with its final scan, and the demodulator's
    /// warm-up makes the frequency stream end slightly before the signal does
    /// — so if the stream runs out inside the last scan, the missing tail is
    /// padded with the last sampled value and the sequence is still returned.
    /// Returns `None` only when the stream ends before every scan was (at
    /// least partially) sampled.
    fn read_sequence<I: Iterator<Item = i16>>(
        &mut self,
        stream: &mut FrequencyStream<I>,
    ) -> Option<SequenceData> {
        let sequence = self.layout.sequences[self.sequence_index];
        let width = self.layout.width;
        let expected_scans = sequence
            .iter()
            .filter(|step| matches!(step, Step::Scan(..)))
            .count();
        let mut t = self.sequence_start;
        let mut scans = Vec::with_capacity(4);
        let mut tone_hz = Vec::with_capacity(4);
        let mut stream_ended = false;

        'steps: for step in sequence {
            match step {
                // Re-align on the actual sync pulse rather than trusting the
                // nominal timing.
                Step::Tone(tone) if tone.frequency == SYNC_FREQUENCY => {
                    if tone.duration.ns() == 0 {
                        continue;
                    }
                    if stream.advance_to(t).is_none() || consume_sync(stream).is_none() {
                        stream_ended = true;
                        break 'steps;
                    }
                    t = stream.position() as f64;
                }
                Step::Tone(tone) => {
                    let len = stream.samples_in(tone.duration);
                    if let Some(frequency) = stream.advance_to(t + len / 2.0) {
                        tone_hz.push(frequency.hz());
                    } else {
                        stream_ended = true;
                        break 'steps;
                    }
                    t += len;
                }
                Step::Scan(channel, duration) => {
                    let len = stream.samples_in(*duration);
                    let pixel_len = len / width as f64;
                    let mut values = vec![0u8; width];
                    let mut last = 0u8;
                    for (x, value) in values.iter_mut().enumerate() {
                        if let Some(sampled) = value_at(stream, t + (x as f64 + 0.5) * pixel_len) {
                            *value = sampled;
                            last = sampled;
                        } else {
                            *value = last;
                            stream_ended = true;
                        }
                    }
                    t += len;
                    scans.push((*channel, values));
                    if stream_ended {
                        break 'steps;
                    }
                }
            }
        }

        if stream_ended && scans.len() < expected_scans {
            return None;
        }
        self.sequence_start = t;
        self.sequence_index = (self.sequence_index + 1) % self.layout.sequences.len();
        Some(SequenceData { scans, tone_hz })
    }
}

/// Consume the sync pulse ahead: skip to its start, then through it.
/// Returns `None` if the stream ends first.
fn consume_sync<I: Iterator<Item = i16>>(stream: &mut FrequencyStream<I>) -> Option<()> {
    while !is_sync(stream.current()) {
        stream.advance()?;
    }
    while is_sync(stream.current()) {
        stream.advance()?;
    }
    Some(())
}

/// The pixel value sampled at a fractional sample position.
fn value_at<I: Iterator<Item = i16>>(stream: &mut FrequencyStream<I>, position: f64) -> Option<u8> {
    let frequency = stream.advance_to(position)?;
    let black = i64::from(BLACK_FREQUENCY.hz());
    let white = i64::from(WHITE_FREQUENCY.hz());
    let value = (i64::from(frequency.hz()) - black) * 255 / (white - black);
    Some(value.clamp(0, 255) as u8)
}

impl<I: Iterator<Item = i16>> Events<I> {
    const fn new(mode: Mode, demodulator: Demodulator<I>) -> Self {
        Self {
            stream: FrequencyStream::new(demodulator),
            requested_mode: mode,
            state: State::Searching,
            queue: VecDeque::new(),
        }
    }

    const fn active_mode(&self) -> Mode {
        match self.requested_mode {
            Mode::Auto => Mode::Robot36,
            mode => mode,
        }
    }

    /// Begin decoding at the first line's timing sequence instead of
    /// searching for a header.
    fn skip_header(&mut self) {
        if matches!(self.state, State::Searching) {
            let image = ImageState::new(self.active_mode(), 0.0);
            self.queue.push_back(Event::ImageStart(image.mode));
            self.state = State::Decoding(image);
        }
    }

    /// Scan for the next image. On success, queue an [`Event::ImageStart`]
    /// and begin decoding; on stream exhaustion, finish.
    fn search(&mut self) {
        self.stream.reset();

        let acquired = if self.requested_mode == Mode::Auto {
            detect_mode(&mut self.stream).or_else(|| detect_by_sync_period(&mut self.stream))
        } else if self.requested_mode.layout().sync_pulse().1.ns() == 0 {
            Some((self.requested_mode, 0.0))
        } else {
            lock_onto_first_line(&mut self.stream, &self.requested_mode.layout())
                .map(|sequence_start| (self.requested_mode, sequence_start))
        };

        match acquired {
            Some((mode, sequence_start)) => {
                // Replay only from the first line onward.
                let origin = self.stream.start_at(sequence_start.max(0.0) as usize);
                let image = ImageState::new(mode, sequence_start - origin as f64);
                self.queue.push_back(Event::ImageStart(image.mode));
                self.state = State::Decoding(image);
            }
            None => self.state = State::Done,
        }
    }

    /// Decode the next timing sequence, queueing its rows. Ends the image with
    /// an [`Event::ImageEnd`] once every line is decoded, or if the signal
    /// runs out mid-image.
    fn decode_step(&mut self) {
        let State::Decoding(image) = &mut self.state else {
            return;
        };

        if image.row_index >= image.layout.height {
            self.queue.push_back(Event::ImageEnd { complete: true });
            self.state = State::Searching;
            return;
        }

        if let Some(data) = image.read_sequence(&mut self.stream) {
            for pixels in image.assembler.assemble(&data) {
                let index = image.row_index;
                image.row_index += 1;
                self.queue.push_back(Event::Row(RgbRow { index, pixels }));
            }
        } else {
            self.queue.push_back(Event::ImageEnd { complete: false });
            self.state = State::Done;
        }
    }
}

impl<I: Iterator<Item = i16>> Iterator for Events<I> {
    type Item = Event;

    fn next(&mut self) -> Option<Event> {
        loop {
            if let Some(event) = self.queue.pop_front() {
                return Some(event);
            }
            match self.state {
                State::Done => return None,
                State::Searching => self.search(),
                State::Decoding(_) => self.decode_step(),
            }
        }
    }
}

/// Whole images assembled from a [`Decoder`]'s event stream, yielded one at
/// a time. Unlike [`Events`], this buffers a full image in memory.
pub struct Images<I: Iterator<Item = i16>> {
    events: Events<I>,
}

impl<I: Iterator<Item = i16>> Iterator for Images<I> {
    type Item = DecodedImage;

    fn next(&mut self) -> Option<DecodedImage> {
        let mode = loop {
            if let Event::ImageStart(mode) = self.events.next()? {
                break mode;
            }
        };
        let width = mode.image_width() as usize;
        let height = mode.image_height() as usize;

        let mut pixels = vec![RgbPixel::new(0, 0, 0); width * height];
        let mut complete = false;
        loop {
            match self.events.next() {
                Some(Event::Row(row)) => {
                    let start = row.index() * width;
                    pixels[start..start + width].copy_from_slice(row.pixels());
                }
                Some(Event::ImageEnd { complete: flag }) => {
                    complete = flag;
                    break;
                }
                Some(Event::ImageStart(_)) | None => break,
            }
        }

        Some(DecodedImage {
            mode,
            width,
            height,
            complete,
            pixels,
        })
    }
}

/// One whole image assembled from a [`Decoder`]'s event stream.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DecodedImage {
    mode: Mode,
    width: usize,
    height: usize,
    complete: bool,
    pixels: Vec<RgbPixel>,
}

impl DecodedImage {
    /// The SSTV mode the image was transmitted in.
    #[must_use]
    pub const fn mode(&self) -> Mode {
        self.mode
    }

    /// The image width in pixels.
    #[must_use]
    pub const fn width(&self) -> usize {
        self.width
    }

    /// The image height in pixels.
    #[must_use]
    pub const fn height(&self) -> usize {
        self.height
    }

    /// Whether every scanline was decoded. `false` if the image was truncated
    /// before completion (for example, the signal faded out).
    #[must_use]
    pub const fn complete(&self) -> bool {
        self.complete
    }

    /// The pixels in row-major order, always `width() * height()` of them.
    /// Rows the signal did not carry are black.
    #[must_use]
    pub fn pixels(&self) -> &[RgbPixel] {
        &self.pixels
    }
}

#[cfg(feature = "image")]
impl From<&DecodedImage> for image::RgbImage {
    // expect: `pixels` always holds `width * height` entries.
    #[allow(clippy::expect_used)]
    fn from(decoded: &DecodedImage) -> Self {
        let mut bytes = Vec::with_capacity(decoded.pixels.len() * 3);
        for pixel in &decoded.pixels {
            bytes.extend_from_slice(&[pixel.red(), pixel.green(), pixel.blue()]);
        }
        Self::from_raw(decoded.width as u32, decoded.height as u32, bytes)
            .expect("pixel count always matches the dimensions")
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec::Vec;

    use super::*;
    use crate::{Encoder, Synthesizer};

    const WIDTH: usize = 320;
    const HEIGHT: usize = 240;

    /// A 320x240 test image with variation in all three channels.
    fn test_image() -> Vec<RgbPixel> {
        let mut pixels = Vec::with_capacity(WIDTH * HEIGHT);
        for y in 0..HEIGHT as u32 {
            for x in 0..WIDTH as u32 {
                let red = (x * 255 / (WIDTH as u32 - 1)) as u8;
                let green = (y * 255 / (HEIGHT as u32 - 1)) as u8;
                let blue = ((x + y) * 255 / (WIDTH as u32 - 1 + HEIGHT as u32 - 1)) as u8;
                pixels.push(RgbPixel::new(red, green, blue));
            }
        }
        pixels
    }

    fn encode(image: &[RgbPixel], sample_rate: u32) -> Vec<i16> {
        // `to_vec` is required: `Encoder::new` needs an owned (`'static`)
        // iterator, so borrowing with `iter().copied()` would not compile.
        #[allow(clippy::unnecessary_to_owned)]
        let encoder = Encoder::new(Mode::Robot36, image.to_vec().into_iter()).unwrap();
        Synthesizer::new(encoder, sample_rate).collect()
    }

    /// Mean absolute per-channel error between two images of equal length.
    fn mean_abs_error(a: &[RgbPixel], b: &[RgbPixel]) -> f64 {
        assert_eq!(a.len(), b.len());
        let total: u64 = a
            .iter()
            .zip(b)
            .map(|(p, q)| {
                let d = |x: u8, y: u8| u64::from((i32::from(x) - i32::from(y)).unsigned_abs());
                d(p.red(), q.red()) + d(p.green(), q.green()) + d(p.blue(), q.blue())
            })
            .sum();
        total as f64 / (a.len() as f64 * 3.0)
    }

    /// The number of samples occupied by our encoder's header at a sample rate.
    fn header_sample_count(sample_rate: u32) -> usize {
        let total_ns: u64 = Mode::Robot36
            .header_tones()
            .map(|tone| tone.duration.ns())
            .sum();
        (total_ns * u64::from(sample_rate) / 1_000_000_000) as usize
    }

    fn assert_matches(decoded: &DecodedImage, image: &[RgbPixel]) {
        assert!(decoded.complete(), "image should decode completely");
        assert_eq!(decoded.pixels().len(), WIDTH * HEIGHT);
        let error = mean_abs_error(image, decoded.pixels());
        assert!(error < 12.0, "mean abs error {error} too high");
    }

    #[test]
    fn round_trip_two_images_back_to_back() {
        let image = test_image();
        let mut samples = encode(&image, 48_000);
        samples.extend(encode(&image, 48_000));

        let decoded: Vec<DecodedImage> =
            Decoder::from_samples(Mode::Robot36, samples.into_iter(), 48_000)
                .images()
                .collect();

        assert_eq!(decoded.len(), 2, "expected two images");
        for decoded_image in &decoded {
            assert_matches(decoded_image, &image);
        }
    }

    #[test]
    fn round_trip_two_images_with_gap() {
        let image = test_image();
        let mut samples = encode(&image, 48_000);
        // Half a second of silence between the two transmissions.
        samples.extend(std::vec![0i16; 24_000]);
        samples.extend(encode(&image, 48_000));

        let decoded: Vec<DecodedImage> =
            Decoder::from_samples(Mode::Robot36, samples.into_iter(), 48_000)
                .images()
                .collect();

        assert_eq!(decoded.len(), 2, "expected two images across the gap");
        for decoded_image in &decoded {
            assert_matches(decoded_image, &image);
        }
    }

    #[test]
    fn round_trip_without_header() {
        let image = test_image();
        // Drop the header so the samples begin at the first line's sync pulse.
        let full = encode(&image, 48_000);
        let header = header_sample_count(48_000);
        let image_samples = full.into_iter().skip(header);

        let mut rows = 0;
        let mut complete = None;
        let mut decoded: Vec<RgbPixel> = Vec::new();
        let events = Decoder::from_samples(Mode::Robot36, image_samples, 48_000)
            .without_header()
            .events();
        for event in events {
            match event {
                Event::ImageStart(mode) => assert_eq!(mode, Mode::Robot36),
                Event::Row(row) => {
                    assert_eq!(row.index(), rows, "row out of order");
                    rows += 1;
                    decoded.extend_from_slice(row.pixels());
                }
                Event::ImageEnd { complete: flag } => complete = Some(flag),
            }
        }

        assert_eq!(complete, Some(true), "image should decode completely");
        assert_eq!(rows, HEIGHT, "should decode all rows");
        let error = mean_abs_error(&image, &decoded);
        assert!(error < 12.0, "mean abs error {error} too high");
    }

    #[test]
    fn auto_detects_mode_when_front_of_header_is_trimmed() {
        let image = test_image();
        let full = encode(&image, 48_000);

        // Emulate a recording clipped at the front (e.g. a trimmed video): drop
        // the VOX tones, first leader and break, leaving the second leader
        // running straight into the VIS bits. Auto detection must still lock on.
        let trimmed: u64 = (0..=9)
            .map(|index| Mode::Robot36.header_tone(index).unwrap().duration.ns())
            .sum();
        let skip = (trimmed * 48_000 / 1_000_000_000) as usize;

        let decoded: Vec<DecodedImage> =
            Decoder::from_samples(Mode::Auto, full.into_iter().skip(skip), 48_000)
                .images()
                .collect();

        assert_eq!(decoded.len(), 1, "expected one image");
        assert_eq!(decoded[0].mode(), Mode::Robot36);
        assert_matches(&decoded[0], &image);
    }

    #[test]
    fn decoding_from_a_demodulator_matches_decoding_from_samples() {
        let image = test_image();
        let samples = encode(&image, 48_000);

        let demodulator = crate::Demodulator::new(samples.clone().into_iter(), 48_000);
        let from_demodulator: Vec<Event> = Decoder::from_demodulator(Mode::Robot36, demodulator)
            .events()
            .collect();
        let from_samples: Vec<Event> =
            Decoder::from_samples(Mode::Robot36, samples.into_iter(), 48_000)
                .events()
                .collect();

        assert_eq!(from_demodulator, from_samples);
    }

    #[test]
    fn silence_yields_no_images() {
        let decoded =
            Decoder::from_samples(Mode::Robot36, std::vec![0i16; 48_000].into_iter(), 48_000)
                .images()
                .next();
        assert!(decoded.is_none(), "silence should not produce an image");
    }
}
