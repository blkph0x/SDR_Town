param([string]$BuildRoot = 'build')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $BuildRoot).Path
$revision = '6ca357c15cbf676ff30eb8eb445d1e1eac17c136'
$source = Join-Path $root 'rtl-driver-source'
if (-not (Test-Path -LiteralPath $source)) {
    git clone https://github.com/pothosware/SoapyRTLSDR.git $source
    if ($LASTEXITCODE -ne 0) { throw 'RTL module clone failed' }
}
$dirty = git -C $source status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'RTL source has local changes; not replacing them' }
git -C $source checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw 'Pinned RTL checkout failed' }
cmake -S $source -B "$root/rtl-driver-build" -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$root/vcpkg_installed/x64-windows"
if ($LASTEXITCODE -ne 0) { throw 'RTL module configure failed' }
if (-not (Select-String "$root/rtl-driver-build/CMakeCache.txt" -Pattern '^HAS_RTLSDR_SET_BIAS_TEE:INTERNAL=1$')) {
    throw 'RTL module requires bias-T API support'
}
cmake --build "$root/rtl-driver-build" --config Release --target rtlsdrSupport -j 4
if ($LASTEXITCODE -ne 0) { throw 'RTL module build failed' }
New-Item -ItemType Directory -Force "$root/bin/Release" | Out-Null
Copy-Item "$root/rtl-driver-build/Release/rtlsdrSupport.dll" "$root/bin/Release/SoapyRTLSDR.dll"
Write-Output "Built pinned SoapyRTLSDR $revision with bias-T support"
