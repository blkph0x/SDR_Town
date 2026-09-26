# 0.2.109 - WFM processing efficiency

WFM's speech channel filter now uses contiguous input history and SIMD across
independent output samples on SSE2 builds, with a scalar fallback elsewhere.
Coefficients, accumulation order, full-rate outputs and power/squelch inputs
are preserved. NFM, P25, HF, RDS processing and Auto BW policy are unchanged.

On the development Windows PC, the existing synthetic10MS/s WFM benchmark
dropped from about2.59 times input duration to0.64, approximately4x faster.
All36 cases retained exactly the same tone/difference/sample-count metrics.
These are demod-call measurements, not total application CPU or a guarantee
for all machines. RDS tap processing is not enabled in that cost benchmark.

Reference-convolution tests cover tiny/large chunks, odd/even filter lengths,
reset, impulse response and tap-length changes. Existing WFM/NFM/RDS continuity
and full application suites remain release gates. Live listening is still a
separate acceptance check;0.2.108 remains available for comparison.
