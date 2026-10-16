$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptDir
try {
    if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
        throw "Node.js and npm are required to build OMG Network Monitor."
    }
    if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
        throw "Rust (cargo) is required to build OMG Network Monitor."
    }

    npm run tauri build
    if ($LASTEXITCODE -ne 0) { throw "Tauri build failed with exit code $LASTEXITCODE." }

    $releaseExe = Join-Path $scriptDir "src-tauri\target\release\omg-network-monitor.exe"
    $portableExe = Join-Path $scriptDir "OMG Network Monitor.exe"
    if (-not (Test-Path -LiteralPath $releaseExe)) {
        throw "Release executable was not found: $releaseExe"
    }
    Copy-Item -LiteralPath $releaseExe -Destination $portableExe -Force
    Write-Host "Portable executable updated: $portableExe"
} finally {
    Pop-Location
}
