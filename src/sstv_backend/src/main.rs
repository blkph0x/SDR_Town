use std::{env, fs, io::{self, BufReader, Read, Write}, path::PathBuf};
use sstv::{Decoder, Encoder, Event, Mode, RgbPixel, Synthesizer};
mod pcm;
use pcm::PcmSamples;

const REVISION: &str = "16bf34aac81b0041f5fdce52a1aef64eea0d5f6e";

fn parse_mode(name: &str) -> Result<Mode, Box<dyn std::error::Error>> {
    Ok(match name {
        "auto" => Mode::Auto,
        "scottie1" => Mode::Scottie1,
        "scottie2" => Mode::Scottie2,
        "scottiedx" => Mode::ScottieDx,
        "martin1" => Mode::Martin1,
        "martin2" => Mode::Martin2,
        "robot36" => Mode::Robot36,
        "robot72" => Mode::Robot72,
        "wrasse180" => Mode::WrasseSc2180,
        "pasokonp3" => Mode::PasokonP3,
        "pasokonp5" => Mode::PasokonP5,
        "pasokonp7" => Mode::PasokonP7,
        "pd50" => Mode::Pd50,
        "pd90" => Mode::Pd90,
        "pd120" => Mode::Pd120,
        "pd160" => Mode::Pd160,
        "pd180" => Mode::Pd180,
        "pd240" => Mode::Pd240,
        "pd290" => Mode::Pd290,
        _ => return Err("unsupported mode".into()),
    })
}

fn mode_name(mode: Mode) -> Result<&'static str, Box<dyn std::error::Error>> {
    Ok(match mode {
        Mode::Auto => "auto",
        Mode::Scottie1 => "scottie1",
        Mode::Scottie2 => "scottie2",
        Mode::ScottieDx => "scottiedx",
        Mode::Martin1 => "martin1",
        Mode::Martin2 => "martin2",
        Mode::Robot36 => "robot36",
        Mode::Robot72 => "robot72",
        Mode::WrasseSc2180 => "wrasse180",
        Mode::PasokonP3 => "pasokonp3",
        Mode::PasokonP5 => "pasokonp5",
        Mode::PasokonP7 => "pasokonp7",
        Mode::Pd50 => "pd50",
        Mode::Pd90 => "pd90",
        Mode::Pd120 => "pd120",
        Mode::Pd160 => "pd160",
        Mode::Pd180 => "pd180",
        Mode::Pd240 => "pd240",
        Mode::Pd290 => "pd290",
        _ => return Err("detected mode is not mapped in this helper".into()),
    })
}

fn run_selftest() -> Result<(), Box<dyn std::error::Error>> {
    const W: usize = 320;
    const H: usize = 240;
    let mut pixels = Vec::with_capacity(W * H);
    for y in 0..H {
        for x in 0..W {
            pixels.push(RgbPixel::new((x * 255 / (W - 1)) as u8, (y * 255 / (H - 1)) as u8, 128));
        }
    }
    let encoder = Encoder::new(Mode::Robot36, pixels.clone().into_iter())?;
    let samples: Vec<i16> = Synthesizer::new(encoder, 48_000).collect();
    let decoded: Vec<_> = Decoder::from_samples(Mode::Auto, samples.into_iter(), 48_000)
        .images()
        .collect();
    if decoded.len() != 1 || decoded[0].mode() != Mode::Robot36 || !decoded[0].complete() {
        return Err("selftest: expected one complete Robot36 image".into());
    }
    println!("{{\"schema\":1,\"backend\":\"{REVISION}\",\"selftest\":true,\"mode\":\"robot36\",\"complete\":true}}");
    Ok(())
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = env::args_os().collect();
    if args.len() == 2 && args[1] == "--version" {
        println!("sdrtown-sstv/1 {REVISION}");
        return Ok(());
    }
    if args.len() == 2 && args[1] == "--selftest" {
        return run_selftest();
    }
    let progress = args.len() == 6 && args[5] == "--progress";
    if args.len() != 5 && !progress {
        return Err("expected input.pcm|--stdin sample-rate output-directory MODE [--progress]".into());
    }
    let rate: u32 = args[2].to_str().ok_or("invalid rate")?.parse()?;
    if !(8000..=96000).contains(&rate) { return Err("sample rate outside 8..96 kHz".into()); }
    let mode = parse_mode(args[4].to_str().ok_or("invalid mode")?)?;
    let limit = u64::from(rate) * 480;
    let (reader, length): (Box<dyn Read>, Option<u64>) = if args[1] == "--stdin" {
        (Box::new(io::stdin()), None)
    } else {
        let file = fs::File::open(PathBuf::from(&args[1]))?;
        let metadata = file.metadata()?;
        let length = metadata.len();
        if !metadata.is_file() || length % 2 != 0 || length > limit * 2 { return Err("invalid PCM length".into()); }
        (Box::new(file), Some(length))
    };
    let output = PathBuf::from(&args[3]);
    fs::create_dir(&output)?;
    let mut samples = PcmSamples::new(BufReader::with_capacity(8192, reader), limit);
    let mut canvas = Vec::<u8>::new();
    let mut seen = Vec::<bool>::new();
    let (mut width, mut height, mut rows, mut count) = (0usize, 0usize, 0usize, 0usize);
    let mut active = false;
    let mut mode_name_s = "";
    for event in Decoder::from_samples(mode, samples.by_ref(), rate).events() {
        match event {
            Event::ImageStart(found) => {
                if active || count >= 4 { return Err("image/session limit exceeded".into()); }
                mode_name_s = mode_name(found)?;
                width = usize::try_from(found.image_width())?;
                height = usize::try_from(found.image_height())?;
                if width == 0 || height == 0 || width > 800 || height > 616 {
                    return Err("invalid dimensions".into());
                }
                canvas = vec![0; width * height * 3];
                seen = vec![false; height]; rows = 0; active = true;
            }
            Event::Row(row) => {
                let y = row.index();
                if !active || y >= height || seen[y] || row.pixels().len() != width {
                    return Err("invalid or duplicate scanline".into());
                }
                for (x, pixel) in row.pixels().iter().enumerate() {
                    canvas[(y*width+x)*3..(y*width+x)*3+3].copy_from_slice(&[pixel.red(),pixel.green(),pixel.blue()]);
                }
                seen[y] = true; rows += 1;
                if progress {
                    use std::fmt::Write as _;
                    let mut hex = String::with_capacity(width * 6);
                    for byte in &canvas[y*width*3..(y+1)*width*3] { write!(&mut hex,"{byte:02x}")?; }
                    println!("{{\"kind\":\"row\",\"schema\":1,\"image\":{count},\"mode\":\"{mode_name_s}\",\"width\":{width},\"height\":{height},\"row\":{y},\"rgb\":\"{hex}\"}}");
                    io::stdout().flush()?;
                }
            }
            Event::ImageEnd { complete } => {
                if !active || (complete && rows != height) { return Err("inconsistent image completion".into()); }
                let file = format!("image-{count}.rgb");
                let mut stream = fs::OpenOptions::new().write(true).create_new(true).open(output.join(&file))?;
                stream.write_all(&canvas)?;
                println!("{{\"schema\":1,\"backend\":\"{REVISION}\",\"file\":\"{file}\",\"mode\":\"{mode_name_s}\",\"width\":{width},\"height\":{height},\"rows\":{rows},\"complete\":{complete}}}");
                io::stdout().flush()?;
                count += 1; active = false;
            }
        }
    }
    let consumed = samples.finish()?;
    if length.is_some_and(|length| length != consumed * 2) { return Err("PCM changed while reading".into()); }
    if active { return Err("unterminated image event stream".into()); }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        let _ = writeln!(io::stderr(), "SSTV backend error: {error}");
        std::process::exit(1);
    }
}
