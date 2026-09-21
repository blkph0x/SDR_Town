# SDR Town 0.2.80 P25 talkgroup hotfix

This hotfix restores P25 talkgroup discovery and early RadioReference alpha-tag display without coupling registry population to the alias database.

## Root cause

Two independent gates made the talkgroup table appear empty or incomplete:

1. A CRC-valid voice grant received before its channel identifier (IDEN) table was treated as a pending grant, but pending grants used the general registry correction budget of 10 corrected dibits. Resolved voice grants used 18. Grants in the 11–18 range were therefore discarded only because IDEN arrived later, so no registry row was created after frequency resolution.
2. RadioReference alias resolution returned no name until WACN/System metadata was known. A newly discovered TGID could therefore have a valid imported alpha tag but show a blank Alpha Tag column during early control-channel acquisition.

## Fix

- Pending unresolved voice grants now use the same correction budget as resolved voice grants.
- When IDEN arrives, retained grants resolve to a voice frequency and are merged into the registry.
- `mergeP25TalkgroupEvent` creates a discovered registry row independently of RadioReference data, so unknown TGIDs remain visible.
- When system metadata is not known, an imported alpha tag is used only if every imported system containing that TGID agrees on the same name. Conflicting mappings remain blank until WACN/System is known.
- A compile-time assertion prevents the pending and resolved voice-grant budgets from drifting apart again.

## Validation

Windows CI builds the MSVC Release application, runs the core unit suite, runs the Qt workspace/alias suite offscreen, performs release-verifier negative tests, creates a clean portable staging tree, and publishes a ZIP plus SHA-256 checksum from `release/*` branches.

Rollback point: `backup/pre-p25-talkgroup-release-20260921`.
