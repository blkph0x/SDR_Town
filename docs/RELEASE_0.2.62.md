# SDR Town 0.2.62 (experimental)

Hotfix for P25 audio stall after large RadioReference alias imports.

`p25EventLogText` no longer re-reads/reparses AppData `p25_aliases.json` on
every RFSS-stamped control event. Site labels use `resolveCachedP25SiteAlias`
with the existing process cache plus mtime/size short-circuit. Decode, grant,
follow, and clear-audio gates are unchanged.

Includes 0.2.61 CSV alias import and status Alpha Tag labels. T-0029 Phase 2
string-verifier CI debt remains documented.
