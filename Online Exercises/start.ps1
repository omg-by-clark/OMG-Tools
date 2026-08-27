param(
  [int]$Port = 8765,
  [switch]$Restart,
  [switch]$NoOpen
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$ServerScript = Join-Path $Root 'local-server-tcp.ps1'
$Url = "http://127.0.0.1:$Port/"

[Console]::OutputEncoding = [Text.Encoding]::UTF8

function Test-OnlineExercisesServer {
  param([string]$TargetUrl)
  try {
    $response = Invoke-WebRequest -Uri $TargetUrl -UseBasicParsing -TimeoutSec 3
    return $response.StatusCode -eq 200
  } catch {
    return $false
  }
}

function Get-ListeningProcess {
  param([int]$TargetPort)
  $connection = Get-NetTCPConnection -LocalPort $TargetPort -State Listen -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $connection) { return $null }
  return Get-CimInstance Win32_Process -Filter "ProcessId=$($connection.OwningProcess)" -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $ServerScript)) {
  throw "Server script not found: $ServerScript"
}

$existing = Get-ListeningProcess -TargetPort $Port
if ($existing) {
  if ((Test-OnlineExercisesServer -TargetUrl $Url) -and -not $Restart) {
    Write-Host "Online Exercises is already running: $Url" -ForegroundColor Green
    if (-not $NoOpen) { Start-Process $Url }
    return
  }

  $commandLine = [string]$existing.CommandLine
  if ($commandLine -notlike '*local-server-tcp.ps1*') {
    Write-Host "Port $Port is used by another program. I did not stop it." -ForegroundColor Red
    Write-Host "Owner: PID $($existing.ProcessId) $($existing.Name)" -ForegroundColor Yellow
    Write-Host "Close that program first, or run this script with -Port <number>." -ForegroundColor Yellow
    exit 1
  }

  Write-Host "Restarting the old Online Exercises server..." -ForegroundColor Yellow
  Stop-Process -Id $existing.ProcessId -Force
  Start-Sleep -Milliseconds 600
}

$arguments = @(
  '-NoProfile',
  '-ExecutionPolicy', 'Bypass',
  '-File', "`"$ServerScript`"",
  '-Root', "`"$Root`"",
  '-Port', $Port
) -join ' '

$process = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -WorkingDirectory $Root -WindowStyle Hidden -PassThru

for ($attempt = 1; $attempt -le 20; $attempt++) {
  if (Test-OnlineExercisesServer -TargetUrl $Url) {
    Write-Host "Online Exercises started: $Url" -ForegroundColor Green
    Write-Host "Server PID: $($process.Id)" -ForegroundColor DarkGray
    if (-not $NoOpen) { Start-Process $Url }
    return
  }
  Start-Sleep -Milliseconds 250
}

Write-Host "Server process started, but the page did not respond yet. PID: $($process.Id)" -ForegroundColor Yellow
Write-Host "Open it manually in a moment: $Url" -ForegroundColor Yellow
