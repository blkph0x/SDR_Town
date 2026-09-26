# 0.2.108 - WFM speech timing

Experimental portable release. WFM speech now keeps causal channel-filter
history, decimation position and PCM time across callbacks. Existing filter
coefficients, de-emphasis and pilot notch are retained. The FIR latency is
(taps-1)/2 input samples, plus two discriminator samples for cubic interpolation.
These delays are reported in the existing bounded FM diagnostics.

Removes missing-lookahead tail repetition and callback-rounded output counts.
Startup fade completes per sample; source changes and reset discard stale speech
history. The previous failing WFM partition characterization is now a mandatory
test with regular/tiny blocks, 44.1/48kHz output and reset/source transitions.

NFM, HF, P25, Auto BW policy and the separate WFM/RDS data branch are unchanged.
RF blocker rejection and physical receiver listening remain separate acceptance
work; passing synthetic tests is not a claim of universal RF performance.
