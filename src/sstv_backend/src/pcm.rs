use std::io::{self, Read};

// DEC-0096: errors cannot be represented by the decoder's i16 iterator. Retain
// them and require finish() before accepting any provisional decoder output.
pub struct PcmSamples<R> {
    reader: R,
    limit: u64,
    count: u64,
    ended: bool,
    error: Option<io::Error>,
}

impl<R: Read> PcmSamples<R> {
    pub fn new(reader: R, limit: u64) -> Self {
        Self { reader, limit, count: 0, ended: false, error: None }
    }

    pub fn finish(self) -> io::Result<u64> {
        if let Some(error) = self.error { return Err(error); }
        if !self.ended {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "PCM was not consumed to EOF"));
        }
        Ok(self.count)
    }
}

impl<R: Read> Iterator for PcmSamples<R> {
    type Item = i16;

    fn next(&mut self) -> Option<i16> {
        if self.ended { return None; }
        let mut bytes = [0u8; 2];
        let mut filled = 0;
        while filled < bytes.len() {
            match self.reader.read(&mut bytes[filled..]) {
                Ok(0) => {
                    self.ended = true;
                    if filled != 0 {
                        self.error = Some(io::Error::new(io::ErrorKind::UnexpectedEof, "odd PCM byte count"));
                    }
                    return None;
                }
                Ok(count) => filled += count,
                Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
                Err(error) => {
                    self.error = Some(error); self.ended = true; return None;
                }
            }
        }
        if self.count == self.limit {
            self.error = Some(io::Error::new(io::ErrorKind::InvalidData, "PCM exceeds session sample budget"));
            self.ended = true; return None;
        }
        self.count += 1;
        Some(i16::from_le_bytes(bytes))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn little_endian_and_exact_budget() {
        let mut pcm = PcmSamples::new(Cursor::new([0, 128, 255, 127, 255, 255]), 3);
        assert_eq!(pcm.by_ref().collect::<Vec<_>>(), [-32768, 32767, -1]);
        assert_eq!(pcm.finish().unwrap(), 3);
    }

    #[test]
    fn empty_and_odd_eof() {
        let mut empty = PcmSamples::new(Cursor::new([]), 0);
        assert_eq!(empty.next(), None); assert_eq!(empty.finish().unwrap(), 0);
        let mut odd = PcmSamples::new(Cursor::new([0, 0, 1]), 2);
        assert_eq!(odd.next(), Some(0)); assert_eq!(odd.next(), None);
        assert_eq!(odd.next(), None);
        assert_eq!(odd.finish().unwrap_err().kind(), io::ErrorKind::UnexpectedEof);
    }

    #[test]
    fn over_budget_and_unconsumed_are_errors() {
        let mut pcm = PcmSamples::new(Cursor::new([0, 0, 0, 0]), 1);
        assert_eq!(pcm.next(), Some(0)); assert_eq!(pcm.next(), None);
        assert_eq!(pcm.finish().unwrap_err().kind(), io::ErrorKind::InvalidData);
        let unfinished = PcmSamples::new(Cursor::new([0, 0]), 1);
        assert_eq!(unfinished.finish().unwrap_err().kind(), io::ErrorKind::InvalidData);
    }

    struct Fragmented { data: Cursor<Vec<u8>>, interrupt: bool }
    impl Read for Fragmented {
        fn read(&mut self, output: &mut [u8]) -> io::Result<usize> {
            self.interrupt = !self.interrupt;
            if self.interrupt { return Err(io::ErrorKind::Interrupted.into()); }
            self.data.read(&mut output[..1])
        }
    }
    #[test]
    fn one_byte_reads_and_interruptions_preserve_samples() {
        let reader = Fragmented { data: Cursor::new(vec![1, 2, 3, 4]), interrupt: false };
        let mut pcm = PcmSamples::new(reader, 2);
        assert_eq!(pcm.by_ref().collect::<Vec<_>>(), [513, 1027]);
        assert_eq!(pcm.finish().unwrap(), 2);
    }

    struct Failed;
    impl Read for Failed {
        fn read(&mut self, _: &mut [u8]) -> io::Result<usize> { Err(io::ErrorKind::BrokenPipe.into()) }
    }
    #[test]
    fn io_failure_is_not_a_successful_eof() {
        let mut pcm = PcmSamples::new(Failed, 3);
        assert_eq!(pcm.next(), None);
        assert_eq!(pcm.finish().unwrap_err().kind(), io::ErrorKind::BrokenPipe);
    }
}
