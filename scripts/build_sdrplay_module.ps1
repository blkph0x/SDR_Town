param(
    [string]$BuildRoot = 'build',
    [string]$ApiRoot = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $BuildRoot).Path
$revision = '48bd8b41072534018de1d74deb3dea5874d9e0e0'
$source = Join-Path $root 'sdrplay-driver-source'
if (-not (Test-Path $source)) {
    git clone https://github.com/pothosware/SoapySDRPlay3.git $source
    if ($LASTEXITCODE -ne 0) { throw 'SoapySDRPlay3 clone failed' }
}
git -C $source checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw 'Pinned SDRplay checkout failed' }
if (-not $ApiRoot) {
    # Same hash-verified development SDK used by the InmarScope build. No install.
    $archive = Join-Path $root 'sdrplay-sdk.zip'
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest https://www.sdrpp.org/SDRplay.zip -OutFile $archive
    }
    if ((Get-FileHash $archive).Hash -ne 'DDB9810B4708B9F53DD7DAD235A7C5AD6990D8D0A0C8A141548D01F80C8C92D0') {
        throw 'SDRplay API 3.15 development SDK checksum mismatch'
    }
    & 7z x $archive "-o$root/sdrplay-sdk" 'SDRplay/API/inc/*' 'SDRplay/API/x64/*.lib' -y
    if ($LASTEXITCODE -ne 0) { throw 'SDRplay development SDK extraction failed' }
    $ApiRoot = Join-Path $root 'sdrplay-sdk/SDRplay/API'
}
$api = (Resolve-Path $ApiRoot).Path
if (-not (Select-String "$api/inc/sdrplay_api.h" -Pattern '^#define\s+SDRPLAY_API_VERSION\s+\(float\)\(3\.15\)')) {
    throw 'This pinned module build requires the qualified API 3.15 development headers'
}
cmake -S $source -B "$root/sdrplay-driver-build" -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$root/vcpkg_installed/x64-windows" `
    "-DLIBSDRPLAY_INCLUDE_DIRS=$api/inc" "-DLIBSDRPLAY_LIBRARIES=$api/x64/sdrplay_api.lib"
if ($LASTEXITCODE -ne 0) { throw 'SDRplay module configure failed' }
cmake --build "$root/sdrplay-driver-build" --config Release --target sdrPlaySupport -j 4
if ($LASTEXITCODE -ne 0) { throw 'SDRplay module build failed' }
New-Item -ItemType Directory -Force "$root/bin/Release" | Out-Null
Copy-Item "$root/sdrplay-driver-build/Release/sdrPlaySupport.dll" "$root/bin/Release/sdrPlaySupport.dll"
Write-Output "Built matched SoapySDRPlay3 $revision (vendor API/service still required)"
