use std::{env, fs, io::{self, Write}, path::PathBuf};
use sstv::{Decoder, Event, Mode};

const REVISION: &str = "16bf34aac81b0041f5fdce52a1aef64eea0d5f6e";

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = env::args_os().collect();
    if args.len() == 2 && args[1] == "--version" {
        println!("sdrtown-sstv/1 {REVISION}");
        return Ok(());
    }
    if args.len() != 5 { return Err("expected input.pcm sample-rate output-directory auto|robot36|martin1".into()); }
    let input = PathBuf::from(&args[1]);
    let rate: u32 = args[2].to_str().ok_or("invalid rate")?.parse()?;
    if !(8000..=96000).contains(&rate) { return Err("sample rate outside 8..96 kHz".into()); }
    let mode = match args[4].to_str() {
        Some("auto") => Mode::Auto,
        Some("robot36") => Mode::Robot36,
        Some("martin1") => Mode::Martin1,
        _ => return Err("unsupported mode".into()),
    };
    let length = fs::metadata(&input)?.len();
    if length % 2 != 0 || length > u64::from(rate) * 360 * 2 { return Err("invalid PCM length".into()); }
    let bytes = fs::read(&input)?;
    if bytes.len() as u64 != length { return Err("PCM changed while reading".into()); }
    let output = PathBuf::from(&args[3]);
    fs::create_dir(&output)?; // Exclusive directory: never overwrite an earlier run.
    let samples = bytes.chunks_exact(2).map(|b| i16::from_le_bytes([b[0], b[1]]));
    let mut canvas = Vec::<u8>::new();
    let mut seen = Vec::<bool>::new();
    let (mut width, mut height, mut rows, mut count) = (0usize, 0usize, 0usize, 0usize);
    let mut active = false;
    let mut mode_name = "";
    for event in Decoder::from_samples(mode, samples, rate).events() {
        match event {
            Event::ImageStart(found) => {
                if active || count >= 4 { return Err("image/session limit exceeded".into()); }
                mode_name = match found {
                    Mode::Robot36 => "robot36",
                    Mode::Martin1 => "martin1",
                    _ => return Err("detected mode is not qualified in this build".into()),
                };
                width = usize::try_from(found.image_width())?;
                height = usize::try_from(found.image_height())?;
                if width != 320 || !(height == 240 || height == 256) { return Err("invalid dimensions".into()); }
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
            }
            Event::ImageEnd { complete } => {
                if !active || (complete && rows != height) { return Err("inconsistent image completion".into()); }
                let file = format!("image-{count}.rgb");
                let mut stream = fs::OpenOptions::new().write(true).create_new(true).open(output.join(&file))?;
                stream.write_all(&canvas)?;
                println!("{{\"schema\":1,\"backend\":\"{REVISION}\",\"file\":\"{file}\",\"mode\":\"{mode_name}\",\"width\":{width},\"height\":{height},\"rows\":{rows},\"complete\":{complete}}}");
                count += 1; active = false;
            }
        }
    }
    if active { return Err("unterminated image event stream".into()); }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        let _ = writeln!(io::stderr(), "SSTV backend error: {error}");
        std::process::exit(1);
    }
}
