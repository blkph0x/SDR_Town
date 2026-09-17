# SDR Town 0.2.60 (experimental)

P25 Calls now has **Aliases...** for named system-specific talkgroup lists.
Create lists and aliases, search by ID/name/group, edit friendly names, and
import/export documented JSON. Imports are reviewed before Save. Manual edits
survive reimport, and existing Alpha Tags take precedence. Known WACN/System ID
and TGID must match; unknown systems never receive a guessed label.

Bounded validation rejects malformed IDs, duplicate entries and oversized files.
Atomic saves reject concurrent database changes. Names are presentation only;
P25 decoding, grant follow and encrypted/wrong-slot audio gates are unchanged.

This is the alias-list workflow, not SDRTrunk XML/RadioReference compatibility.
CSV, radio-ID aliases and log/transcript labels remain future work. No real
talkgroup directory is bundled. See docs/P25_ALIASES.md for format and setup.
Existing SSTV RF acceptance and P25 QA limitations remain documented.
