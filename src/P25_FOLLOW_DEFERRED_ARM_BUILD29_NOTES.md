# P25 Follow Deferred Arm Build 29

Problem observed: app still crashes/hangs immediately when auto-follow accepts a talkgroup grant from a monitored control channel.

Change:
- Retune to the voice frequency first.
- Do not synchronously reset/construct the voice decoder and clear audio buffers in the same control-channel grant handler stack frame.
- Voice decode is armed after a short 650 ms settle window via `QTimer::singleShot`.
- The delayed arm validates that the app is still following the same TG and voice frequency before touching receiver/decoder state.
- Manual TG follow uses the same deferred arm path.

Intent:
- Separate CC grant processing, Soapy retune, UI state changes, and P25 voice decoder reset into safer phases.
- Avoid stale CC worker/grant state re-entering the follow path during the exact retune/decoder transition.

Safety:
- Does not weaken encryption/audio gates.
- Phase 2 audio remains closed until MAC/ESS proves clear.
