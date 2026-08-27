$ErrorActionPreference = 'Stop'

$compiler = Get-Command g++.exe -ErrorAction Stop
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ideRoot = Join-Path $root 'OMG-IDE'
$source = Join-Path $ideRoot 'OMG IDE.cpp'
$output = Join-Path $ideRoot 'OMG-IDE.exe'
$include = Join-Path $ideRoot 'scintilla-include'

& $compiler.Source -std=c++17 -O2 -Wall -Wextra -municode -mwindows "-I$include" $source -o $output -lcomctl32 -lcomdlg32 -lgdiplus
if ($LASTEXITCODE -ne 0) {
    throw "IDE build failed with exit code $LASTEXITCODE"
}

Write-Host "Built: $output"
