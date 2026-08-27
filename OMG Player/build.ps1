$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $scriptDir "OMGPlayer.cpp"
$resourceScript = Join-Path $scriptDir "OMGPlayer.rc"
$resourceObject = Join-Path $scriptDir "OMGPlayer.res"
$output = Join-Path $scriptDir "OMG Player.exe"

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
    $windresFallback = Join-Path (Split-Path -Parent $gppPath) "windres.exe"
    if (Test-Path -LiteralPath $windresFallback) {
        $windres = Get-Item -LiteralPath $windresFallback
    }
}

$resourceArgs = @()
if ($windres -and (Test-Path -LiteralPath $resourceScript)) {
    $windresPath = if ($windres.Source) { $windres.Source } else { $windres.FullName }
    Push-Location $scriptDir
    try {
        & $windresPath -O coff "OMGPlayer.rc" -o "OMGPlayer.res"
        if ($LASTEXITCODE -ne 0) {
            throw "windres failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
    $resourceArgs = @($resourceObject)
}

& $gppPath `
    -std=c++17 `
    -O2 `
    -Wall `
    -Wextra `
    -municode `
    -mwindows `
    -o $output `
    $source `
    @resourceArgs `
    -lmfplay `
    -lmfplat `
    -lmfuuid `
    -luuid `
    -lole32 `
    -loleaut32 `
    -lcomctl32 `
    -lcomdlg32 `
    -lshell32

if ($LASTEXITCODE -ne 0) {
    throw "g++ failed with exit code $LASTEXITCODE."
}

Write-Host "Built: $output"
