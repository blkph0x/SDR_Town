# 0.2.106 - NFM PCM sample clock

Experimental portable testing release, building on user-confirmed clear NFM in
0.2.105. No auto-bandwidth or classifier adjustment was made.

- NFM audio sample timing now follows the continuous discriminator clock rather
  than rounded callback sizes. Existing cubic interpolation is retained with a
  fixed two-input-sample delay and four-sample history; no repeated tail samples.
- NFM startup fade completes per sample, not per callback. Its 6 ms time constant
  is unchanged. Reset/rate transitions do not retain previous output-filter state.
- Adds maxPcmDelayUs and hintMismatchBlocks to bounded diagnostics. A hint mismatch
  is not a lost sample: callback output counts are hints, as in the HF path.
- P25, WFM/HF processing, channel filters, bandwidth selection and squelch policy
  are unchanged. WFM boundary and NFM blocker-rejection work remain separate.

Tests compare actual NFM PCM at 48 kHz/2.048/2.4/10 MS/s input and 44.1/48 kHz
output, using whole and tiny irregular blocks. Exact counts, waveform error below
1e-5, zero unavailable lookahead reads and zero phase repairs are required.
Physical listening across receivers remains tester acceptance, not inferred from
these fixtures. No signed installer/updater channel replacement is included.
