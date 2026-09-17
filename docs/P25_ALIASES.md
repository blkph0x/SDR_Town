# P25 Alias Lists

Open **P25 Calls > Aliases...**. Use **New list** with a name, WACN and
System ID (hexadecimal), then **Add alias** with a decimal talkgroup ID and
friendly name. **Edit alias** changes its name/group. Search matches ID, name
or group. Review the list and **Save**; **Cancel** discards all staged changes.
The list applies across control channels with matching decoded system metadata.
Hover the Alpha Tag column for the registry's WACN/System ID or database errors.
Unknown system identity does not match imported names. No frequency fallback.

This follows SDRTrunk's separation of aliases from decoding, not full playlist
compatibility: https://github.com/DSheirer/sdrtrunk/wiki/Playlist-Editor#aliases
The current GUI resolves names in the P25 talkgroup table only. Log/transcript
and radio-ID aliases, SDRTrunk XML, CSV, ranges, colors and RadioReference account
integration are not included yet. Names are user-supplied metadata, not received
proof of affiliation, encryption, activity or audio eligibility.

## Import and Updates

Import accepts one system list as UTF-8 JSON. Use the fictional example below
as a format reference, not a real radio directory. Numeric JSON values are
decimal, including WACN/System ID (the GUI displays those two in hex).

```json
{
  "version": 1,
  "wacn": 781824,
  "systemId": 1,
  "name": "Example system",
  "source": "Fictional example - replace with your source",
  "updated": "2026-09-18",
  "talkgroups": [
    {"id": 123, "name": "Example Dispatch", "group": "Example agency"}
  ]
}
```

WACN:0..1048575, System ID:0..4095, TGID:1..65535. One list per
WACN/System ID. Duplicate talkgroups/systems, invalid values and malformed files
are rejected. Limit:1MiB per file/database,10000 entries per list. Names/groups
are limited to128 characters; source to512; date must be YYYY-MM-DD. Group may
be empty but must be a string. Unsupported control characters are rejected.

Reimport replaces the imported part of the same system's list, including removal
of imported IDs omitted by the new file. Aliases added/edited in this manager
are manual overrides and survive reimport, even if absent from the new file.
Existing **Add TG > Alpha Tag** names take precedence over all list aliases.
Remove list deletes the selected list only after confirmation and Save; legacy
Alpha Tags remain unchanged. Export writes a single list; when another user
imports it the exported manual flags are not trusted as local overrides.

Source/date describe the supplied list, not independent verification or the last
manual edit. No database subscription or automatic downloading is included.
Use only lists you are allowed to use/share and check their system IDs.

## Storage and Safety

Lists are stored separately in the application's AppData `p25_aliases.json`.
Save uses an interprocess lock, original-file comparison and atomic QSaveFile
replacement. Corrupt files are reported, never silently replaced. If another
process changes the database, reopen the editor before saving. Export cannot
target the active database. No aliases modify P25 grants, follow priorities,
scanner membership, vocoder state or security/slot gates.

Developer gate: `sdr_town_workspace_tests.exe "[aliases]"`; set
`SDR_TOWN_ALIAS_SCREENSHOT` to save the tested compact dialog preview.
