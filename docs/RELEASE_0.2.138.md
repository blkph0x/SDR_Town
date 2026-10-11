# 0.2.138: companion radio selection and secure local pairing

Paired companion: FUBAR 1.1.50. Experimental release; P25 decoder, vocoder,
speaker path, follow timing and gain are unchanged.

- Web Tune, mode and Monitor P25 can explicitly select a stable radio identity.
- Stale/missing selections fail before frequency/mode/squelch changes. There is
  no silent fallback to another radio and no override of a named workflow.
- Status exposes selected identity and a useful selection error. The website
  preserves the operator's draft selection across polling and unplug events.
- Ordinary Windows launches provision one protected per-user pairing credential;
  SdrTownControl.dll reads it automatically only for literal 127.0.0.1. No token
  is sent to web browsers or telemetry. CLI/environment token overrides remain.
- Authentication stays required; health alone is public. Corrupt protected
  credentials fail closed, not silently reset. Same-account applications share
  this capability; this is not protection against malicious code in that account.

Update both applications and the bundled DLL. Run them under the same Windows
account. On FUBAR: Take control, choose Radio for tuning, Tune & receive.
If a workflow owns that radio, stop it locally or choose a free radio. A physical
radio change is refused while the primary receiver is in P25.

For explicit-token integrations use --control-token / SDR_TOWN_CONTROL_TOKEN
and set the DLL caller token accordingly. Non-Windows servers still require an
explicit token. Local pairing stores DPAPI ciphertext under
HKCU\Software\SDR_Town\ControlPairingV1; no plaintext shared secret is packaged.

Validation and public-delivery evidence: docs/BUILD_NOTES.md (DEC-0221).
Previously audited RX cancellation/activation defects remain ISS-0105/0106;
this is not a claim those independent device-lifecycle issues are repaired.
