# 0.2.110 - NFM image rejection

NFM input above300kS/s now uses a causal Kaiser FIR before its first downsample,
replacing the single moving average. It computes only retained outputs and keeps
the existing decimation counts and downstream speech/tone pipeline. This targets
strong signals that previously folded into the wanted channel.

At2.4MS/s with the benchmark's +40dB image blocker, wanted audio gain improved
from-5.55dB to approximately0dB; test-versus-clean audio difference improved
from+3.23dB to about-72.49dB. This is synthetic IQ, not a universal RF claim.
Sampled filter response is <0.1dB variation through12.5kHz and >80dB rejection
over the specified stopband grid. First/second image-blocker tests cover both
signs of folded offsets at2.4/10MS/s.

First-stage causal delay is8*M input samples, about42us at common rates, and is
included in the existing FM delay diagnostics. CPU rises versus the moving
average;10MS/s measured about0.27 times input duration on the development PC.
WFM, P25, HF, bandwidth policy and downstream NFM settings are unchanged.
NFM below/equal300kS/s retains its prior input filter. Physical listening and
front-end overload behavior remain receiver-specific acceptance work.
