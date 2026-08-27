$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $scriptDir "FileViewer.cpp"
$output = Join-Path $scriptDir "File Viewer.exe"
$iconDir = "C:\Catppuccin-Icons\Theme\CatppuccinIcon"
$iconCache = Join-Path $scriptDir "icon-cache\96"

[array]$nodeCandidates = @(
    "C:\Users\35727\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe",
    (Get-Command node -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

if ($nodeCandidates.Count -gt 0 -and (Test-Path -LiteralPath $iconDir)) {
    $nodePath = $nodeCandidates[0]
    $env:NODE_PATH = "C:\Users\35727\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules"
    & $nodePath (Join-Path $scriptDir "render-icons.js") $iconDir $iconCache 96
    if ($LASTEXITCODE -ne 0) {
        throw "SVG icon rendering failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Warning "Node.js or Catppuccin icon directory was not found; the app will fall back to its built-in SVG renderer."
}

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

& $gppPath `
    -std=c++17 `
    -O2 `
    -Wall `
    -Wextra `
    -municode `
    -mwindows `
    -finput-charset=UTF-8 `
    -fexec-charset=UTF-8 `
    -D_WIN32_WINNT=0x0601 `
    -o $output `
    $source `
    -lcomctl32 `
    -lcomdlg32 `
    -lgdiplus `
    -lshell32 `
    -lole32 `
    -luuid `
    -luxtheme

if ($LASTEXITCODE -ne 0) {
    throw "g++ failed with exit code $LASTEXITCODE."
}

Write-Host "Built: $output"
