# Pinned SGP4 core

Upstream: https://github.com/aholinch/sgp4
Commit: 552cb1489a52c3023ae70cb6c7e239e84c5950fe
Files: src/cpp/SGP4.c, src/cpp/SGP4.h, LICENSE (Unlicense).
Local portability change: upstream's custom `fmod` function and all its calls
are renamed `sgp4_mod` to avoid defining an MSVC/libc intrinsic. Its mathematical
behavior is unchanged. The .c file is compiled as C++ as upstream documents.
No upstream TLE wrapper is used. See DEC-0112 and src/Sgp4.cpp.
Verification vectors: https://celestrak.org/publications/AIAA/2006-6753/
