use core::fmt;

#[derive(Debug)]
/// An error generated while encoding or decoding.
pub enum Error {
    /// Emitted if now enough pixels could be fetched from the image.
    ///
    /// ```rust
    /// use sstv::{Encoder, Error, Mode, RgbPixel};
    ///
    /// let empty_image: Vec<RgbPixel> = vec![];
    ///
    /// assert!(matches!(
    ///     Encoder::new(Mode::Robot36, empty_image.into_iter()),
    ///     Err(Error::EmptyImage)
    /// ));
    /// ```
    EmptyImage,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyImage => write!(
                f,
                "The supplied image is empty. Was the pixel iterator already used?"
            ),
        }
    }
}

impl core::error::Error for Error {}

/// Result with the custom sstv Error.
pub type Result<T> = core::result::Result<T, Error>;
