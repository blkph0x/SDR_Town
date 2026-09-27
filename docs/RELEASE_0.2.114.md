# SDR Town 0.2.114 - Continuous SSTV autosave

Experimental portable testing release. Extract the complete ZIP to a fresh folder.

## Unattended SSTV reception

- SSTV remembers the Save folder, defaulting to Pictures/SDR Town/SSTV.
- Receive and Decode create a unique timestamped session folder automatically.
- Live reception saves each decoded image as PNG immediately, with a JSON sidecar.
- Live no longer stops after four images or eight minutes. Temporary RGB files
  are removed after verification and saving. Only the last 64 image descriptions
  are retained; all saved images remain on disk.
- Stop receiving finishes the current partial image, labelled `.partial.png`.
  Cancel discards the unfinished image but preserves already saved images.
- Device changes, input discontinuities and save failures stop reception with an
  error rather than silently joining unrelated data. Saved images are not deleted
  automatically: allow sufficient disk space for unattended reception.

Tune the receiver first, open SSTV, choose Live RF and Receive. For a known NFM
channel, explicit NFM avoids waiting for the VIS header needed by RF Auto. The
Image format can still remain Automatic. Auto RF selects one route per session;
this release does not scan or switch modulation between transmissions.

File decoding keeps its existing duration/image limits. No P25, NFM, WFM,
SDRplay or Inmarsat DSP changes. No claim of physical RF validation: the
automated live test uses a virtual NFM device driven by recorded SSTV audio.

This portable prerelease is not a signed stable updater package.
