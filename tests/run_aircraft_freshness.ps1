param(
    [string]$QtRoot = 'C:/Qt/6.11.1/msvc2022_64',
    [string]$VcRoot = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207',
    [string]$SdkVersion = '10.0.26100.0',
    [string]$Filter = '[adsb]'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $env:TEMP ('sdr-aircraft-freshness-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$deps = Join-Path $root 'build/vcpkg_installed/x64-windows'
$env:PATH = "$VcRoot/bin/Hostx64/x64;$QtRoot/bin;$deps/bin;$env:PATH"
$env:INCLUDE = "$VcRoot/include;$sdk/Include/$SdkVersion/ucrt;$sdk/Include/$SdkVersion/shared;$sdk/Include/$SdkVersion/um"
$env:LIB = "$VcRoot/lib/x64;$sdk/Lib/$SdkVersion/ucrt/x64;$sdk/Lib/$SdkVersion/um/x64"
$sources = @('tests/aircraft_freshness_main.cpp', 'tests/test_adsb.cpp',
    'src/AdsBTrackStore.cpp', 'src/ModeS.cpp', 'src/SatPassPlanner.cpp',
    'src/SatCatalogue.cpp', 'src/SatObserverConfig.cpp', 'src/TleStore.cpp',
    'src/Sgp4.cpp', 'src/HttpGet.cpp') | ForEach-Object { Join-Path $root $_ }
# No shared build writes. SGP4 is unchanged and read from the configured build.
$arguments = @('/nologo', '/std:c++20', '/Zc:__cplusplus', '/EHsc', '/MD', '/O2',
    '/utf-8', '/DNOMINMAX', '/DQT_CORE_LIB', '/DSPDLOG_SHARED_LIB',
    '/DSPDLOG_COMPILED_LIB', '/DSPDLOG_FMT_EXTERNAL', '/DFMT_SHARED',
    "/I$root/include", "/I$deps/include", "/I$QtRoot/include", "/I$QtRoot/include/QtCore",
    "/I$QtRoot/mkspecs/win32-msvc", "/Fo$out/", "/Fe$out/aircraft_freshness.exe") +
    $sources + @('/link', "/LIBPATH:$QtRoot/lib", "/LIBPATH:$deps/lib", 'Qt6Core.lib',
    'Catch2.lib', 'spdlog.lib', 'fmt.lib', "$root/build/Release/sdr_town_sgp4.lib", 'winhttp.lib')
Write-Output "ISOLATED_OUTPUT=$out"
& "$VcRoot/bin/Hostx64/x64/cl.exe" @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$out/aircraft_freshness.exe" $Filter
exit $LASTEXITCODE
