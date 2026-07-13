# Build Fix 19A — DeviceInfo index compile fix

Fixes the MSVC compile error introduced in buildfix19:

```text
DeviceManager.cpp(116): error C2039: index is not a member of DeviceInfo
```

The Soapy open fallback now uses only fields available in the current header:

1. driver + serial, when serial exists
2. driver only

It intentionally avoids label/hardware display fields as Device::make kwargs, and avoids DeviceInfo::index because this source package is header-compatible with your existing repo.

Waterfall behavior from buildfix19 is preserved: fallback stub IQ no longer emits a strong moving carrier that can hide the fact that real hardware did not open.
