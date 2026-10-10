# SDR Town 0.2.136 experimental

## You can hear SSTV while it waits for a picture

Live SSTV on a chosen radio now plays the tones on your selected audio
outputs. If the only output is a VB-Audio cable, the default speakers are
opened as well and the cable stays on, so other SSTV programs can still
decode it.

The SSTV window says where the sound is going. HF SSTV is usually USB.
VHF SSTV is usually NFM. The picture appears when a VIS header is found.

P25 control mute no longer silences USB, LSB, AM, or CW. It still silences
NFM and WFM. A receiver that is already decoding P25 voice is not shared
with SSTV.

This build also includes the DVB-S2 work since 0.2.135-experimental:
QPSK, 8PSK, 16APSK, and 32APSK frames can be decoded from symbols or from
root-raised-cosine samples at a known symbol rate. The app does not decode
pictures. Commercial decrypt is not performed.

This is an experimental portable build. It does not replace the signed
0.2.134 installer or the in-app updater.
