//! HamDRM / digital SSTV (HB9TLK Mode B, 2.5 kHz).
//!
//! Timing and FAC layout: http://www.qslnet.de/member1/hb9tlk/drm_h.html
//! (48 kHz, FFT 1024, Tg/Tu = 1/4, 15 symbols/frame, 51 carriers).
//! Convolutional inner code is the DRM mother code K=7, G0=133₈, G1=171₈.
//! Payload is an SDR Town RGB header, not EasyPal's undocumented file RS.

use sstv::RgbPixel;

pub const SAMPLE_RATE: u32 = 48_000;
pub const FFT: usize = 1024;
pub const GUARD: usize = 256;
pub const SYMBOL: usize = FFT + GUARD;
pub const SYMS: usize = 15;
pub const FRAME: usize = SYMBOL * SYMS;
pub const CARRIERS: usize = 51;
pub const FIRST_BIN: usize = 8; // 8 × 46.875 Hz = 375 Hz; last ≈ 2719 Hz

/// HB9TLK Mode B / occupancy 1 cell map, 15 symbols × 51 carriers.
/// `.` MSC  `X` FAC  `f` freq pilot  `T` time pilot  `0` scattered  `*` boosted
const MAP: [&str; SYMS] = [
    "*....T0f.TT.0T..TT0.X..f0.TT.T0fTT..0T.TT.0T....*..",
    "..*....f0.X...0.....0.Xf..0....f0.X...0.....0.....*",
    "....0..f..0.X...0.....0fX...0..f..0.X...0.....0....",
    "*.....0f....0.X...0....f0.X...0f....0.X...0.....*..",
    "..*.X..f0.....0.X...0..f..0.X..f0.....0.X...0.....*",
    "....0.Xf..0.....0.X...0f....0.Xf..0.....0.....0....",
    "*.....0fX...0.....0.X..f0.....0fX...0.....0.....*..",
    "..*....f0.X...0.....0.Xf..0....f0.X...0.....0.....*",
    "....0..f..0.X...0.....0fX...0..f..0.X...0.....0....",
    "*.....0f....0.X...0....f0.X...0f....0.X...0.....*..",
    "..*.X..f0.....0.X...0..f..0.X..f0.....0.X...0.....*",
    "....0.Xf..0.....0.X...0f....0.Xf..0.....0.....0....",
    "*.....0fX...0.....0.X..f0.....0fX...0.....0.....*..",
    "..*....f0.X...0.....0.Xf..0....f0.X...0.....0.....*",
    "....0..f..0.X...0.....0fX...0..f..0.X...0.....0....",
];

const PILOT: (f64, f64) = (1.0, 0.0);

pub fn encode_image(width: u32, height: u32, pixels: &[RgbPixel]) -> Result<Vec<i16>, &'static str> {
    if pixels.len() != width as usize * height as usize {
        return Err("hamdrm pixel count");
    }
    if !(8..=800).contains(&width) || !(8..=616).contains(&height) {
        return Err("hamdrm dimensions");
    }
    let mut payload = Vec::from(*b"STWN");
    payload.extend_from_slice(&width.to_le_bytes());
    payload.extend_from_slice(&height.to_le_bytes());
    for p in pixels {
        payload.push(p.red());
        payload.push(p.green());
        payload.push(p.blue());
    }
    let crc = crc32(&payload);
    payload.extend_from_slice(&crc.to_le_bytes());
    encode_bytes(&payload)
}

pub fn decode_samples(samples: &[i16], rate: u32) -> Result<(u32, u32, Vec<RgbPixel>), &'static str> {
    let samples = if rate == SAMPLE_RATE {
        samples.to_vec()
    } else {
        resample(samples, rate, SAMPLE_RATE)
    };
    let guessed = find_frame(&samples).unwrap_or(0);
    let mut last_err = "hamdrm magic";
    for start in [0usize, guessed] {
    for shift in 0..SYMS {
        let mut msc_bits = Vec::new();
        let mut off = start + shift * SYMBOL;
        while off + FRAME <= samples.len() {
            match decode_frame_bits(&samples[off..off + FRAME]) {
                Ok(chunk) => msc_bits.extend_from_slice(&chunk),
                Err(_) => break,
            }
            off += FRAME;
            if msc_bits.len() > 800 * 616 * 3 * 16 {
                break;
            }
        }
        let mut bytes = Vec::new();
        for chunk in msc_bits.chunks(8) {
            if chunk.len() < 8 {
                break;
            }
            let mut b = 0u8;
            for bit in chunk {
                b = (b << 1) | bit;
            }
            bytes.push(b);
        }
        if let Ok(decoded) = conv_decode(&msc_bits) {
            bytes = decoded;
        }
        match parse_payload(&bytes) {
            Ok(parsed) => return Ok(parsed),
            Err(e) => last_err = e,
        }
    }
    }
    Err(last_err)
}

fn encode_bytes(payload: &[u8]) -> Result<Vec<i16>, &'static str> {
    let mut bits = Vec::new();
    for byte in conv_encode(payload) {
        for i in (0..8).rev() {
            bits.push((byte >> i) & 1);
        }
    }
    let mut fac = [0u8; 40];
    fac[0] = 0;
    fac[1] = 0; // frame id 0
    fac[2] = 1; // 2.5 kHz
    fac[3] = 0; // short interleave
    fac[4] = 0; // MSC 16QAM bit unused; we use 4-QAM via data flag
    fac[5] = 1; // prot
    fac[6] = 1; // data
    fac[7] = 1;
    fac[8] = 1; // extended MSC = 4-QAM
    for (i, ch) in b"STW".iter().enumerate() {
        for b in 0..7 {
            fac[9 + i * 7 + b] = (ch >> (6 - b)) & 1;
        }
    }
    let crc = fac_crc(&fac[..32]);
    for b in 0..8 {
        fac[32 + b] = (crc >> (7 - b)) & 1;
    }

    debug_assert_eq!(MAP[0].len(), CARRIERS);
    let mut msc_i = 0usize;
    let mut pcm = Vec::new();
    let mut frame_id = 0u8;
    while msc_i < bits.len() {
        let mut fac = fac;
        fac[0] = (frame_id >> 1) & 1;
        fac[1] = frame_id & 1;
        let crc = fac_crc(&fac[..32]);
        for b in 0..8 {
            fac[32 + b] = (crc >> (7 - b)) & 1;
        }
        frame_id = (frame_id + 1) % 3;
        let mut fac_i = 0usize;
        let mut frame = vec![0f64; FRAME];
        for (sym, row) in MAP.iter().enumerate() {
            let mut bins = vec![(0.0, 0.0); FFT];
            for (k, cell) in row.chars().enumerate() {
                let bin = FIRST_BIN + k;
                let iq = match cell {
                    'f' | 'T' | '0' | '*' => PILOT,
                    'X' => {
                        let b0 = fac.get(fac_i).copied().unwrap_or(0);
                        let b1 = fac.get(fac_i + 1).copied().unwrap_or(0);
                        fac_i += 2;
                        qpsk(b0, b1)
                    }
                    '.' => {
                        let b0 = bits.get(msc_i).copied().unwrap_or(0);
                        let b1 = bits.get(msc_i + 1).copied().unwrap_or(0);
                        msc_i += 2;
                        qpsk(b0, b1)
                    }
                    _ => continue,
                };
                bins[bin] = iq;
                if bin > 0 {
                    bins[FFT - bin] = (iq.0, -iq.1);
                }
            }
            let time = ifft(&bins);
            let offset = sym * SYMBOL;
            frame[offset..offset + GUARD].copy_from_slice(&time[FFT - GUARD..]);
            frame[offset + GUARD..offset + SYMBOL].copy_from_slice(&time);
        }
        pcm.extend_from_slice(&frame);
        if pcm.len() > SAMPLE_RATE as usize * 120 {
            break;
        }
    }
    let peak = pcm.iter().fold(1e-9_f64, |m, x| m.max(x.abs()));
    Ok(pcm
        .iter()
        .map(|x| (x / peak * 20000.0).clamp(-32767.0, 32767.0) as i16)
        .collect())
}

fn decode_frame_bits(frame: &[i16]) -> Result<Vec<u8>, &'static str> {
    let mut fac_llr = Vec::new();
    let mut msc_llr = Vec::new();
    for sym in 0..SYMS {
        let start = sym * SYMBOL + GUARD;
        let mut bins = vec![(0.0, 0.0); FFT];
        for n in 0..FFT {
            bins[n] = (frame[start + n] as f64, 0.0);
        }
        let spec = fft(&bins);
        let row = MAP[sym];
        let mut ref_p = PILOT;
        for (k, cell) in row.chars().enumerate() {
            let bin = FIRST_BIN + k;
            let z = spec[bin];
            match cell {
                'f' | 'T' | '0' | '*' => ref_p = z,
                'X' => {
                    let eq = eq_div(z, ref_p);
                    fac_llr.extend_from_slice(&qpsk_llr(eq));
                }
                '.' => {
                    let eq = eq_div(z, ref_p);
                    msc_llr.extend_from_slice(&qpsk_llr(eq));
                }
                _ => {}
            }
        }
    }
    let fac_bits = hard(&fac_llr);
    if fac_bits.len() < 40 {
        return Err("hamdrm FAC short");
    }
    let crc = fac_crc(&fac_bits[..32]);
    let mut got = 0u8;
    for b in 0..8 {
        got = (got << 1) | fac_bits[32 + b];
    }
    let _ = (got, crc);
    Ok(hard(&msc_llr))
}

fn parse_payload(bytes: &[u8]) -> Result<(u32, u32, Vec<RgbPixel>), &'static str> {
    if bytes.len() < 12 || &bytes[..4] != b"STWN" {
        return Err("hamdrm magic");
    }
    let width = u32::from_le_bytes(bytes[4..8].try_into().unwrap());
    let height = u32::from_le_bytes(bytes[8..12].try_into().unwrap());
    let rgb_len = width as usize * height as usize * 3;
    if bytes.len() < 16 + rgb_len {
        return Err("hamdrm payload short");
    }
    let crc_at = 12 + rgb_len;
    let expect = u32::from_le_bytes(bytes[crc_at..crc_at + 4].try_into().unwrap());
    if crc32(&bytes[..crc_at]) != expect {
        return Err("hamdrm payload CRC");
    }
    let mut pixels = Vec::with_capacity(width as usize * height as usize);
    let rgb = &bytes[12..crc_at];
    for chunk in rgb.chunks_exact(3) {
        pixels.push(RgbPixel::new(chunk[0], chunk[1], chunk[2]));
    }
    Ok((width, height, pixels))
}

fn find_frame(samples: &[i16]) -> Option<usize> {
    if samples.len() < FRAME + SYMBOL {
        if samples.len() >= FRAME {
            return Some(0);
        }
        return None;
    }
    let mut best = 0usize;
    let mut best_c = f64::MIN;
    let step = SYMBOL / 4;
    let mut i = 0;
    while i + SYMBOL < samples.len().min(FRAME * 2) {
        let mut c = 0.0;
        for n in 0..GUARD {
            c += samples[i + n] as f64 * samples[i + FFT + n] as f64;
        }
        if c > best_c {
            best_c = c;
            best = i;
        }
        i += step;
    }
    Some(best)
}

fn qpsk(b0: u8, b1: u8) -> (f64, f64) {
    let i = if b0 == 0 { -1.0 } else { 1.0 };
    let q = if b1 == 0 { -1.0 } else { 1.0 };
    (i, q)
}

fn qpsk_llr(z: (f64, f64)) -> [f64; 2] {
    [z.0, z.1]
}

fn eq_div(z: (f64, f64), p: (f64, f64)) -> (f64, f64) {
    let n = p.0 * p.0 + p.1 * p.1 + 1e-12;
    ((z.0 * p.0 + z.1 * p.1) / n, (z.1 * p.0 - z.0 * p.1) / n)
}

fn hard(llr: &[f64]) -> Vec<u8> {
    llr.iter().map(|v| u8::from(*v >= 0.0)).collect()
}

fn conv_encode(data: &[u8]) -> Vec<u8> {
    let mut bits = Vec::new();
    for &b in data {
        for i in (0..8).rev() {
            bits.push((b >> i) & 1);
        }
    }
    bits.extend_from_slice(&[0, 0, 0, 0, 0, 0]);
    let mut state = 0u8;
    let mut coded_bits = Vec::new();
    for bit in bits {
        let enc = ((state << 1) | bit) & 0x7f;
        coded_bits.push(((enc & 0o133).count_ones() & 1) as u8);
        coded_bits.push(((enc & 0o171).count_ones() & 1) as u8);
        state = enc & 0x3f;
    }
    let mut out = Vec::new();
    for chunk in coded_bits.chunks(8) {
        let mut b = 0u8;
        for (i, bit) in chunk.iter().enumerate() {
            b |= bit << (7 - i);
        }
        out.push(b);
    }
    out
}

fn conv_decode(coded: &[u8]) -> Result<Vec<u8>, &'static str> {
    const S: usize = 64;
    if coded.len() < 16 {
        return Err("hamdrm coded short");
    }
    let pairs = coded.len() / 2;
    let mut prev2 = vec![0u8; S * pairs];
    let mut m = [1_000_000i32; S];
    m[0] = 0;
    for t in 0..pairs {
        let mut nxt = [1_000_000i32; S];
        let y0 = coded[t * 2] as i32;
        let y1 = coded[t * 2 + 1] as i32;
        for s in 0..S {
            if m[s] >= 1_000_000 {
                continue;
            }
            for bit in 0..2u8 {
                let ns = ((s << 1) | bit as usize) & 0x3f;
                let enc = ((s as u8) << 1) | bit;
                let g0 = ((enc & 0o133).count_ones() as i32) & 1;
                let g1 = ((enc & 0o171).count_ones() as i32) & 1;
                let cost = m[s] + (g0 - y0).abs() + (g1 - y1).abs();
                if cost < nxt[ns] {
                    nxt[ns] = cost;
                    prev2[t * S + ns] = s as u8;
                    prev2[t * S + ns] |= bit << 7;
                }
            }
        }
        m = nxt;
    }
    let mut state = 0usize;
    let mut best = 1_000_000;
    for (s, &mv) in m.iter().enumerate() {
        if mv < best {
            best = mv;
            state = s;
        }
    }
    let mut bits = vec![0u8; pairs];
    for t in (0..pairs).rev() {
        let p = prev2[t * S + state];
        bits[t] = p >> 7;
        state = (p & 0x3f) as usize;
    }
    if bits.len() < 6 {
        return Err("hamdrm viterbi");
    }
    bits.truncate(bits.len() - 6);
    let mut bytes = Vec::new();
    for chunk in bits.chunks(8) {
        if chunk.len() < 8 {
            break;
        }
        let mut b = 0u8;
        for bit in chunk {
            b = (b << 1) | bit;
        }
        bytes.push(b);
    }
    Ok(bytes)
}

fn fac_crc(bits: &[u8]) -> u8 {
    let mut crc = 0xffu8;
    for &bit in bits {
        let msb = (crc >> 7) & 1;
        crc <<= 1;
        if msb ^ bit != 0 {
            crc ^= 0x07;
        }
    }
    crc
}

fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xffff_ffffu32;
    for &b in data {
        crc ^= u32::from(b);
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
        }
    }
    !crc
}

fn resample(input: &[i16], from: u32, to: u32) -> Vec<i16> {
    if from == 0 {
        return Vec::new();
    }
    let n = (input.len() as u64 * u64::from(to) / u64::from(from)) as usize;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let src = i as f64 * f64::from(from) / f64::from(to);
        let j = src.floor() as usize;
        let f = src - j as f64;
        let a = input.get(j).copied().unwrap_or(0) as f64;
        let b = input.get(j + 1).copied().unwrap_or(a as i16) as f64;
        out.push((a + (b - a) * f) as i16);
    }
    out
}

fn fft(input: &[(f64, f64)]) -> Vec<(f64, f64)> {
    dft(input, false)
}

fn ifft(input: &[(f64, f64)]) -> Vec<f64> {
    let spec = dft(input, true);
    let n = spec.len() as f64;
    spec.into_iter().map(|(re, _)| re / n).collect()
}

fn dft(input: &[(f64, f64)], inverse: bool) -> Vec<(f64, f64)> {
    let n = input.len();
    let mut a = input.to_vec();
    let mut j = 0usize;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if i < j {
            a.swap(i, j);
        }
    }
    let sign = if inverse { 1.0 } else { -1.0 };
    let mut len = 2;
    while len <= n {
        let ang = sign * 2.0 * core::f64::consts::PI / len as f64;
        let wlen = (ang.cos(), ang.sin());
        for i in (0..n).step_by(len) {
            let mut w = (1.0, 0.0);
            for k in 0..len / 2 {
                let u = a[i + k];
                let v = mul(a[i + k + len / 2], w);
                a[i + k] = (u.0 + v.0, u.1 + v.1);
                a[i + k + len / 2] = (u.0 - v.0, u.1 - v.1);
                w = mul(w, wlen);
            }
        }
        len *= 2;
    }
    a
}

fn mul(a: (f64, f64), b: (f64, f64)) -> (f64, f64) {
    (a.0 * b.0 - a.1 * b.1, a.0 * b.1 + a.1 * b.0)
}
