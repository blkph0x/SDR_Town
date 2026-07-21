# Install / refresh the project-local LIVE STT venv (faster-whisper).
# Usage (from repo root):
#   powershell -ExecutionPolicy Bypass -File .\scripts\setup_stt.ps1
#
# Then either:
#   - Launch SDR_Town from the repo (auto-discovers .venv-stt), or
#   - setx SDR_TOWN_STT_PYTHON "C:\path\to\maulaudio_pro\.venv-stt\Scripts\python.exe"
# Open Tools → Decode Log / Transcript… and confirm status "STT ready".

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$venv = Join-Path $root ".venv-stt"
$pyCandidates = @(
    "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
    "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe",
    "$env:LOCALAPPDATA\Programs\Python\Python311\python.exe"
)
$basePy = $null
foreach ($c in $pyCandidates) {
    if (Test-Path $c) { $basePy = $c; break }
}
if (-not $basePy) {
    $basePy = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
}
if (-not $basePy) { throw "No Python 3.11+ found. Install Python 3.12 x64 first." }

Write-Host "Base Python: $basePy"
if (-not (Test-Path (Join-Path $venv "Scripts\python.exe"))) {
    & $basePy -m venv $venv
}
$venvPy = Join-Path $venv "Scripts\python.exe"
& $venvPy -m pip install --upgrade pip
& $venvPy -m pip install faster-whisper
$env:SDR_TOWN_WHISPER_MODEL = "base.en"
$env:SDR_TOWN_WHISPER_DEVICE = "cpu"
$env:SDR_TOWN_WHISPER_COMPUTE = "int8"
& $venvPy -c "from faster_whisper import WhisperModel; WhisperModel('base.en', device='cpu', compute_type='int8'); print('model_ready')"
Write-Host ""
Write-Host "STT venv ready: $venvPy"
Write-Host "Optional persistent env:"
Write-Host "  setx SDR_TOWN_STT_PYTHON `"$venvPy`""
Write-Host "  setx SDR_TOWN_STT_SCRIPT `"$(Join-Path $root 'scripts\stt_transcribe_wav.py')`""
