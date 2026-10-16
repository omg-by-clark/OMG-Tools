param([switch]$Install)
$ErrorActionPreference = 'Stop'
$npm = (Get-Command npm.cmd -ErrorAction SilentlyContinue).Source
if (!$npm) { $npm = 'C:\Program Files\nodejs\npm.cmd' }
if (!(Test-Path -LiteralPath $npm)) { throw 'Node.js LTS (including npm) is required.' }
if ($Install -or !(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'node_modules'))) {
    & $npm install
    if ($LASTEXITCODE -ne 0) { throw "Dependency installation failed: $LASTEXITCODE" }
}

$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path -LiteralPath $cargoBin) { $env:Path = "$cargoBin;$env:Path" }
$tauriCli = Join-Path $PSScriptRoot 'node_modules\.bin\tauri.cmd'
if (!(Test-Path -LiteralPath $tauriCli)) { throw 'The local Tauri CLI is missing. Run build.ps1 -Install first.' }

# Keep all Rust build artifacts on D: so every build reuses the same cache.
# Changing CARGO_TARGET_DIR creates a separate Cargo cache and causes a slow,
# effectively cold rebuild.
$buildRoot = 'D:\OMG-Tauri-Build'
$tempRoot = Join-Path $buildRoot 'tmp'
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
$env:CARGO_TARGET_DIR = Join-Path $buildRoot 'target'
$env:TEMP = $tempRoot
$env:TMP = $tempRoot
$env:CARGO_HTTP_MULTIPLEXING = 'false'
$env:CARGO_NET_RETRY = '5'

# Keep the language runtime beside the application resources, so packaged IDEs
# execute the same OMG interpreter as the command-line tool.
$interpreter = Join-Path $PSScriptRoot '..\..\OMG Lang\omg-lang\tools\omg.exe'
if (!(Test-Path -LiteralPath $interpreter)) { throw "OMG interpreter was not found: $interpreter" }
$resourceDir = Join-Path $PSScriptRoot 'src-tauri\resources'
New-Item -ItemType Directory -Force -Path $resourceDir | Out-Null
Copy-Item -LiteralPath $interpreter -Destination (Join-Path $resourceDir 'omg.exe') -Force

# Tauri's Windows linker needs the MSVC developer environment.  Use it when
# installed, while still allowing an already configured developer shell.
$vsDevCmd = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($vsDevCmd) {
    # cmd.exe has a long-standing parsing edge case with the parentheses in
    # "Program Files (x86)".  Resolve its 8.3 path before entering cmd.
    $vsDevCmdShort = (& cmd.exe /d /c ('for %I in ("' + $vsDevCmd + '") do @echo %~sI')).Trim()
    # Call the project-local CLI directly. Going through `npm run` here can
    # leave npm spinning before Cargo starts when invoked from another cmd.
    $buildCommand = 'call ' + $vsDevCmdShort + ' -arch=x64 -host_arch=x64 >nul && ' + $tauriCli + ' build'
    & cmd.exe /d /c $buildCommand
} else {
    & $tauriCli build
}
if ($LASTEXITCODE -ne 0) { throw "Tauri build failed: $LASTEXITCODE" }
