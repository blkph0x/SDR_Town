# CW / Morse reception

Open **Tools > CW / Morse Decoder**. This is an experimental receive-only
decoder; no transmit or radio-control command is derived from decoded text.

## Main receiver

1. Start a real SDR with **Tune and Receive**, then select CW, USB, LSB, AM,
   NFM or WFM explicitly. AUTO must first be resolved by the operator.
2. Tune the signal into the receiver passband. Ordinary keyed carriers use CW
   (700 Hz beat note) or a suitable SSB offset. NFM/WFM/AM can carry keyed audio
   tones; FM does not create a tone from an unmodulated CW carrier.
3. Select **Main receiver**, then **Start**. Pitch and speed default to Auto;
   manual settings accept 200-1200 Hz and 5-55 WPM.
4. **Stop** ends only Morse observation. Closing the window also cancels its
   worker. Changing receiver frequency/mode/bandwidth/device stops the session
   with a reason; start again on the new source.

P25 and simulated sources are refused. The decoder owns a separate chronological
IQ cursor and demodulator; it does not consume the speaker ring, change device
ownership, retune, or mute/unmute another workflow. Speaker volume/squelch do not
gate this independent data decoder. Physical SDRplay diversity composite input
is not yet qualified and is not accepted as direct live hardware.

## Recordings and diagnostics

Select **Audio recording** and open WAV, FLAC or MP3. Stereo audio is mixed to
mono; limits are 256 MiB and 30 minutes. The backend needs acquisition history;
very short transmissions, speech, noise and hand-keying can decode incorrectly.
At EOF only, the analysis history is drained with silence to release final text.
This is not generated speaker audio. Files stay local; no transcript or audio is
automatically sent to the diagnostics server.

The window displays estimated pitch/speed, sample count, IQ gaps, recognition
resets and invalid blocks. Start/stop/error and aggregate processing counters
join the existing application log. Copy/save are explicit local actions;
transcript history is capped at 16 KiB. Tone/speed preferences persist.

Core: [GGMorse](https://github.com/ggerganov/ggmorse), pinned MIT source with
documented JFFT/MSVC/no-stdout integration changes. See
`external/ggmorse/README.sdr-town.md`. Timing fixture follows
[ITU-R M.1677-1](https://www.itu.int/rec/R-REC-M.1677-1-200910-I).

Synthetic tests are not on-air acceptance. Report frequency/mode, build,
estimated pitch/speed, gap/reset counts and a short reviewed recording for
failed live tests. Do not infer station identity from uncertain text.
