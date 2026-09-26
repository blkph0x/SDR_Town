# SDR Town 0.2.99 - Aero rate-to-frequency selection

Changing the Aero decoder rate now previews a matching frequency from the
selected satellite's dated survey. An already matching center is preserved;
otherwise the first surveyed channel for that rate/mode is selected. Tune or
Start applies the selection to reception. Merely opening/restoring the window
never overrides a saved frequency or retunes the radio.

For APAC presets the first centers are 1546.0050 MHz / 10500 bit/s,
1542.9350 MHz / 8400 bit/s, 1545.0700 MHz / 1200 bit/s and
1545.0250 MHz / 600 bit/s. These are discrete reference channels, not universal
frequency ranges. Other channels are in the Band plan tab; manual tuning and
waterfall selection remain available. A new manual frequency clears the preset
preview. Existing watch lists are preserved.

Burst modes, EGC and satellite/rate pairs without a surveyed channel retain the
frequency and show a no-preset status instead of inventing an assignment. In
particular, no obsolete 6F1 voice frequency is supplied. Reference data remains
the dated survey documented in data/inmarsat/README.md, not a live RF survey.

Retains 0.2.98's matched SDRplay plugin; official API 3.15+ and service required.
Extract the complete portable ZIP. This is a regular GitHub testing release,
not a signed installer/in-app update. P25 processing is unchanged.
