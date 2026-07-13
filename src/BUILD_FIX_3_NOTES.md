# Build Fix 3

Fixed MSVC compile error in `main.cpp` caused by two `const double elapsedSeconds` declarations in the same `pollLiveIqCapture()` scope.

The first variable measures GUI poll interval for dynamic IQ ring draining. The second has been renamed to `captureSeconds` and is used only for live status/health reporting.
