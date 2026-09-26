# Inmarsat L-band band plans (SDR Town)

Classic Aero frequency facts from the [Wilson/Sergi.vdl2 survey published by
thebaldgeek](https://raw.githubusercontent.com/thebaldgeek/thebaldgeek.github.io/828314e0512604879df6828ee5cbda2d02f9ea6d/L-Band.md).
February 2024 entries are historical references; 6F1 uses its April 2025 update,
excluding the explicitly obsolete voice list. APAC was labelled 4F1 in that
survey: these are regional reference channels, not newly verified 4F2 traffic.
Verify actual channels/spot beams locally. No frequency is inferred from a rate.
Unsupported STD-C presets and the old evenly spaced invented channels were
removed in 0.2.98. Rate is bit/s; the legacy JSON key remains `baud` for API
compatibility. User watch lists remain independent and are not overwritten.

Not derived from InmarScope GPL bandplan JSON files.

| File | Satellite | Slot | Region |
| --- | --- | --- | --- |
| `i4a.json` | I4A / Alphasat | 25 E | EMEA |
| `4f2.json` | I-4 F2 | 143.5 E | APAC |
| `4f3.json` | I-4 F3 | 98 W | AMER |
| `6f1.json` | I-6 F1 | 83.5 E | IOE |
| `3f5.json` | I-3 F5 | 54 W | AORE |
