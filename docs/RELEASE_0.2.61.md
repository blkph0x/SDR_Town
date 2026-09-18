# SDR Town 0.2.61 (experimental)

RadioReference-compatible **Import CSV...** for talkgroup and site alias lists
(TG-SITES / `trs_tg_*` / `trs_sites_*` style). Destination WACN/System ID is
chosen explicitly; Mode/NAC/frequencies never affect RF or security policy.
Duplicate site rows merge display names. AppData load prefers the Qt
AppDataLocation path (Roaming on Windows) and migrates a Local copy if needed.

P25 status text now shows Alpha Tag / imported alias when system metadata is
known (for example `TG 10120 Dispatch Decoding`). Presentation only.

P25 grant/follow/vocoder and clear-audio gates are unchanged. Existing Phase 2
string-verifier CI debt (T-0029) and SSTV RF acceptance gaps remain documented.
See docs/P25_ALIASES.md.
