# SDR Town project instructions

Before changing this project, read `SOURCE_OF_TRUTH.md`, `DEVELOPMENT_RULES.md`
and the current entries in `docs/TASKS.md`, `docs/ISSUES.md` and
`docs/DECISIONS.md`. Keep code, tests and the engineering trackers aligned.

## Standing delivery requirement

Completed working application changes must be pushed to GitHub and released
through GitHub Actions without waiting for another publish request. Follow
`DEVELOPMENT_RULES.md` section 11: wait for relevant Actions runs, repair any
failures, verify the public release and downloaded assets, and smoke-test the
shipped executable. A local build or a private Actions artifact is not enough.
Do not publish broken builds or claim success while checks are pending.
Preserve release-channel/updater safety and never upload private signing keys.
An explicit later instruction to pause or not publish overrides this default.
