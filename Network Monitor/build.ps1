$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $scriptDir "NetworkMonitor.cpp"
$output = Join-Path $scriptDir "Network Monitor.exe"
$resourceScript = Join-Path $scriptDir "NetworkMonitor.rc"
$resourceObject = Join-Path $scriptDir "NetworkMonitor.res"

$gpp = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $gpp) {
    $fallback = "C:\Users\35727\Desktop\A-MyProject\c++_compile\mingw64\bin\g++.exe"
    if (Test-Path -LiteralPath $fallback) {
        $gpp = Get-Item -LiteralPath $fallback
    } else {
        throw "g++ was not found. Install MinGW-w64 or add g++ to PATH."
    }
}
$gppPath = if ($gpp.Source) { $gpp.Source } else { $gpp.FullName }

$windres = Get-Command windres -ErrorAction SilentlyContinue
if (-not $windres) {
    $candidate = Join-Path (Split-Path -Parent $gppPath) "windres.exe"
    if (Test-Path -LiteralPath $candidate) { $windres = Get-Item -LiteralPath $candidate }
}
$resourceArgs = @()
if ($windres -and (Test-Path -LiteralPath $resourceScript)) {
    $windresPath = if ($windres.Source) { $windres.Source } else { $windres.FullName }
    Push-Location $scriptDir
    try {
        & $windresPath -O coff "NetworkMonitor.rc" -o "NetworkMonitor.res"
        if ($LASTEXITCODE -ne 0) { throw "windres failed with exit code $LASTEXITCODE." }
    } finally { Pop-Location }
    $resourceArgs = @($resourceObject)
}

& $gppPath `
    -std=c++17 `
    -O2 `
    -Wall `
    -Wextra `
    -municode `
    -mwindows `
    -D_WIN32_WINNT=0x0601 `
    -o $output `
    $source `
    @resourceArgs `
    -liphlpapi `
    -lws2_32 `
    -lcomctl32 `
    -lcomdlg32 `
    -ldwmapi `
    -luxtheme `
    -lgdiplus `
    -lversion `
    -lshell32 `
    -ladvapi32

if ($LASTEXITCODE -ne 0) {
    throw "g++ failed with exit code $LASTEXITCODE."
}

Write-Host "Built: $output"
