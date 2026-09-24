use std::{env, fs, io::{self, BufReader, Read, Write}, path::PathBuf};
use sstv::{Decoder, Encoder, Event, Mode, RgbPixel, Synthesizer};
mod hamdrm;
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
        "robotbw8" => Mode::RobotBw8,
        "robotbw12" => Mode::RobotBw12,
        "robot24" => Mode::Robot24,
        "sc230" => Mode::WrasseSc230,
        "sc260" => Mode::WrasseSc260,
        "sc2120" => Mode::WrasseSc2120,
        "avt24" => Mode::Avt24,
        "avt90" => Mode::Avt90,
        "avt94" => Mode::Avt94,
        "avt188" => Mode::Avt188,
        "martin3" => Mode::Martin3,
        "martin4" => Mode::Martin4,
        "scottie3" => Mode::Scottie3,
        "scottie4" => Mode::Scottie4,
        "fax480" => Mode::Fax480,
        "sc124" => Mode::WrasseSc124,
        "sc148" => Mode::WrasseSc148,
        "sc148q" => Mode::WrasseSc148q,
        "sc196" => Mode::WrasseSc196,
        "mp73" => Mode::Mp73,
        "mp115" => Mode::Mp115,
        "mp140" => Mode::Mp140,
        "mp175" => Mode::Mp175,
        "mr73" => Mode::Mr73,
        "mr90" => Mode::Mr90,
        "mr115" => Mode::Mr115,
        "mr140" => Mode::Mr140,
        "ml180" => Mode::Ml180,
        "ml240" => Mode::Ml240,
        "ml280" => Mode::Ml280,
        "ml320" => Mode::Ml320,
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
        Mode::RobotBw8 => "robotbw8",
        Mode::RobotBw12 => "robotbw12",
        Mode::Robot24 => "robot24",
        Mode::WrasseSc230 => "sc230",
        Mode::WrasseSc260 => "sc260",
        Mode::WrasseSc2120 => "sc2120",
        Mode::Avt24 => "avt24",
        Mode::Avt90 => "avt90",
        Mode::Avt94 => "avt94",
        Mode::Avt188 => "avt188",
        Mode::Martin3 => "martin3",
        Mode::Martin4 => "martin4",
        Mode::Scottie3 => "scottie3",
        Mode::Scottie4 => "scottie4",
        Mode::Fax480 => "fax480",
        Mode::WrasseSc124 => "sc124",
        Mode::WrasseSc148 => "sc148",
        Mode::WrasseSc148q => "sc148q",
        Mode::WrasseSc196 => "sc196",
        Mode::Mp73 => "mp73",
        Mode::Mp115 => "mp115",
        Mode::Mp140 => "mp140",
        Mode::Mp175 => "mp175",
        Mode::Mr73 => "mr73",
        Mode::Mr90 => "mr90",
        Mode::Mr115 => "mr115",
        Mode::Mr140 => "mr140",
        Mode::Ml180 => "ml180",
        Mode::Ml240 => "ml240",
        Mode::Ml280 => "ml280",
        Mode::Ml320 => "ml320",
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

fn mean_abs_error(a: &[RgbPixel], b: &[RgbPixel]) -> f64 {
    let total: u64 = a.iter().zip(b).map(|(p, q)| {
        let d = |x: u8, y: u8| u64::from((i32::from(x) - i32::from(y)).unsigned_abs());
        d(p.red(), q.red()) + d(p.green(), q.green()) + d(p.blue(), q.blue())
    }).sum();
    total as f64 / (a.len() as f64 * 3.0)
}

fn run_selftest_all() -> Result<(), Box<dyn std::error::Error>> {
    const RATE: u32 = 16_000;
    const MAX_ERR: f64 = 12.0;
    let mut failed = 0usize;
    let mut passed = 0usize;
    for mode in Mode::ALL {
        let w = mode.image_width() as usize;
        let h = mode.image_height() as usize;
        let mut pixels = Vec::with_capacity(w * h);
        for y in 0..h {
            for x in 0..w {
                pixels.push(RgbPixel::new(
                    (x * 255 / w.saturating_sub(1).max(1)) as u8,
                    (y * 255 / h.saturating_sub(1).max(1)) as u8,
                    128,
                ));
            }
        }
        let encoder = Encoder::new(mode, pixels.clone().into_iter())?;
        let mut samples: Vec<i16> = Synthesizer::new(encoder, RATE).collect();
        samples.extend(std::iter::repeat_n(0i16, RATE as usize / 2));
        let mut ok = true;
        for decoder_mode in [mode, Mode::Auto] {
            let decoded: Vec<_> = Decoder::from_samples(decoder_mode, samples.iter().copied(), RATE)
                .images()
                .collect();
            if decoded.len() != 1 || decoded[0].mode() != mode || !decoded[0].complete() {
                ok = false;
                eprintln!("SSTV selftest-all fail: {mode:?} via {decoder_mode:?} count/mode/complete");
                continue;
            }
            let expected: Vec<RgbPixel> = match mode {
                Mode::RobotBw8 | Mode::RobotBw12 | Mode::Robot24 | Mode::Fax480 => pixels.iter().copied().map(|p| {
                    let y = sstv::YuvPixel::from(p).luma();
                    RgbPixel::new(y, y, y)
                }).collect(),
                _ => pixels.clone(),
            };
            let err = mean_abs_error(&expected, decoded[0].pixels());
            if err >= MAX_ERR {
                ok = false;
                eprintln!("SSTV selftest-all fail: {mode:?} via {decoder_mode:?} mae={err}");
            }
        }
        if ok { passed += 1; } else { failed += 1; }
    }
    println!("{{\"schema\":1,\"backend\":\"{REVISION}\",\"selftestAll\":true,\"passed\":{passed},\"failed\":{failed},\"modes\":{}}}", Mode::ALL.len());
    if failed > 0 { return Err("selftest-all: one or more modes failed".into()); }
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
    if args.len() == 2 && args[1] == "--selftest-all" {
        return run_selftest_all();
    }
    if args.len() == 2 && args[1] == "--selftest-hamdrm" {
        return run_selftest_hamdrm();
    }
    let progress = args.len() == 6 && args[5] == "--progress";
    if args.len() != 5 && !progress {
        return Err("expected input.pcm|--stdin sample-rate output-directory MODE [--progress]".into());
    }
    let rate: u32 = args[2].to_str().ok_or("invalid rate")?.parse()?;
    if !(8000..=96000).contains(&rate) { return Err("sample rate outside 8..96 kHz".into()); }
    let mode_arg = args[4].to_str().ok_or("invalid mode")?;
    if mode_arg == "hamdrm" {
        return run_hamdrm_file(&args);
    }
    let mode = parse_mode(mode_arg)?;
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
    if count == 0 && mode == Mode::Auto && args[1] != "--stdin" {
        return write_hamdrm_image(PathBuf::from(&args[1]), rate, PathBuf::from(&args[3]), progress);
    }
    Ok(())
}

fn run_hamdrm_file(args: &[std::ffi::OsString]) -> Result<(), Box<dyn std::error::Error>> {
    if args[1] == "--stdin" { return Err("digital STWN prototype is file-only".into()); }
    let rate: u32 = args[2].to_str().ok_or("invalid rate")?.parse()?;
    if !(8000..=96000).contains(&rate) { return Err("sample rate outside 8..96 kHz".into()); }
    let output = PathBuf::from(&args[3]);
    fs::create_dir(&output)?;
    write_hamdrm_image(PathBuf::from(&args[1]), rate, output, args.len() == 6)
}

fn write_hamdrm_image(pcm: PathBuf, rate: u32, output: PathBuf, progress: bool) -> Result<(), Box<dyn std::error::Error>> {
    let mut bytes = Vec::new();
    fs::File::open(&pcm)?.take(u64::from(rate) * 480 * 2 + 1).read_to_end(&mut bytes)?;
    if bytes.len() as u64 > u64::from(rate) * 480 * 2 { return Err("PCM budget exceeded".into()); }
    if bytes.len() % 2 != 0 { return Err("invalid PCM length".into()); }
    let samples: Vec<i16> = bytes.chunks_exact(2).map(|c| i16::from_le_bytes([c[0], c[1]])).collect();
    let (width, height, pixels) = hamdrm::decode_samples(&samples, rate).map_err(|e| e.to_string())?;
    if width == 0 || height == 0 || width > 800 || height > 616 {
        return Err("invalid dimensions".into());
    }
    let mut canvas = vec![0u8; width as usize * height as usize * 3];
    for (i, p) in pixels.iter().enumerate() {
        canvas[i * 3..i * 3 + 3].copy_from_slice(&[p.red(), p.green(), p.blue()]);
    }
    let file = "image-0.rgb";
    fs::OpenOptions::new().write(true).create_new(true).open(output.join(file))?.write_all(&canvas)?;
    if progress {
        use std::fmt::Write as _;
        for (y, row) in canvas.chunks_exact(width as usize * 3).enumerate() {
            let mut hex = String::with_capacity(row.len() * 2);
            for byte in row { write!(&mut hex, "{byte:02x}")?; }
            println!("{{\"kind\":\"row\",\"schema\":1,\"image\":0,\"mode\":\"hamdrm\",\"width\":{width},\"height\":{height},\"row\":{y},\"rgb\":\"{hex}\"}}");
        }
    }
    println!("{{\"schema\":1,\"backend\":\"{REVISION}\",\"file\":\"{file}\",\"mode\":\"hamdrm\",\"width\":{width},\"height\":{height},\"rows\":{height},\"complete\":true}}");
    io::stdout().flush()?;
    Ok(())
}

fn run_selftest_hamdrm() -> Result<(), Box<dyn std::error::Error>> {
    const W: u32 = 32;
    const H: u32 = 32;
    let mut pixels = Vec::new();
    for y in 0..H {
        for x in 0..W {
            pixels.push(RgbPixel::new((x * 8) as u8, (y * 8) as u8, 64));
        }
    }
    let wav = hamdrm::encode_image(W, H, &pixels).map_err(|e| e.to_string())?;
    let (w, h, out) = hamdrm::decode_samples(&wav, hamdrm::SAMPLE_RATE).map_err(|e| e.to_string())?;
    if w != W || h != H || out.len() != pixels.len() {
        return Err("hamdrm selftest size".into());
    }
    let err: u64 = pixels.iter().zip(&out).map(|(a, b)| {
        let d = |x: u8, y: u8| u64::from((i32::from(x) - i32::from(y)).unsigned_abs());
        d(a.red(), b.red()) + d(a.green(), b.green()) + d(a.blue(), b.blue())
    }).sum();
    let mae = err as f64 / (out.len() as f64 * 3.0);
    if mae > 20.0 {
        return Err(format!("hamdrm selftest mae {mae}").into());
    }
    println!("{{\"schema\":1,\"backend\":\"{REVISION}\",\"selftestHamdrm\":true,\"mae\":{mae}}}");
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        let _ = writeln!(io::stderr(), "SSTV backend error: {error}");
        std::process::exit(1);
    }
}
