# SDR Town 0.2.100 - Inmarsat constellation selection

Fixes a saved watch-channel selection remaining pinned after manual tuning.
The manual decoder uses a different identity, which previously left its diagram
blank even when fresh symbols were available. Successful Tune, manual Start and
band-plan preset tuning now select the current decoder's constellation.

The diagram labels the actual decoder frequency and bit rate. Explicit watch
selection stays isolated: a channel outside the current watch group shows
"Not in active group", never another channel's symbols. Quiet bursts may have
no fresh points; EGC has no native constellation path. Dots alone do not prove
protocol lock. Merely changing a rate/frequency preview does not retune RF.

Regression tests cover manual/preset tuning after watch selection, switching
active channels, clearing old symbols and real modem scatter callbacks at
600/1200/8400/10500 bit/s. Synthetic test input proves display plumbing, not
live satellite reception. Decoder DSP, scheduling, audio and P25 are unchanged.

Retains matched SDRplay support (official API 3.15+ and service required).
Extract the entire portable ZIP. Regular GitHub testing release, not a signed
installer or in-app updater replacement.
