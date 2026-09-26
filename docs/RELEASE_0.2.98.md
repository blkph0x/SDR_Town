# SDR Town 0.2.98 - SDRplay and Inmarsat preset repair

Portable testing release, published by GitHub Actions. Extract the complete
ZIP into a new folder; do not copy just the executable. Not a signed installer
or in-app updater package. Existing signed 0.2.96 remains the updater release.

## Repairs

- Bundles SoapySDRPlay3 0.5.2 (48bd8b4), built against this application's Soapy
  runtime, with its MIT licence. Like InmarScope, the proprietary SDRplay API
  3.15+ and running service must be installed from the vendor separately.
- Accepts SDRPLAY_API_PATH as a full API DLL override for nonstandard installs.
- Replaces incorrect interpolated Inmarsat frequencies with dated published
  channel centers/rates for APAC, Americas, Atlantic East, Alphasat and 6F1.
  APAC presets are historical regional references, not current RF verification.
- Removes the invented missing-file fallback and obsolete 6F1 voice presets.
  MHz now displays four decimal places; rates are labelled bit/s, not baud.
- Retains RTL bias-T controls from 0.2.97: select the RTL row in Device Manager.
  Do not enable antenna power without confirming the hardware/antenna is DC-safe.

Existing user watch lists are preserved. Replace incorrect saved frequencies
manually using the corrected preset table or your verified local observations.

P25 decoder/audio code is unchanged. This release does not claim to resolve
the separately reported missed grants/early cut-offs without a matching capture.
RSPdx reception and physical bias-T operation still require tester confirmation.
