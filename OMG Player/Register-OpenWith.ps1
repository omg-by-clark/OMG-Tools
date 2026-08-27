$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $scriptDir "OMG Player.exe"

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "OMG Player.exe was not found. Build it first with .\build.ps1."
}

$appExeName = "OMG Player.exe"
$progId = "OMGPlayer.media"
$extensions = @(
    ".mp4", ".m4v", ".mov", ".mkv", ".avi", ".wmv", ".mpg", ".mpeg",
    ".mp3", ".wav", ".wma", ".aac", ".m4a", ".flac", ".ogg"
)

function Ensure-Key($path) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -Path $path -Force | Out-Null
    }
}

function Set-DefaultValue($path, $value) {
    Ensure-Key $path
    Set-Item -LiteralPath $path -Value $value
}

$appRoot = "HKCU:\Software\Classes\Applications\$appExeName"
$command = "`"$exePath`" `"%1`""
$appPathRoot = "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\$appExeName"

Ensure-Key $appRoot
New-ItemProperty -LiteralPath $appRoot -Name "FriendlyAppName" -Value "OMG Player" -PropertyType String -Force | Out-Null
Set-DefaultValue "$appRoot\shell\open\command" $command

Ensure-Key $appPathRoot
Set-Item -LiteralPath $appPathRoot -Value $exePath
New-ItemProperty -LiteralPath $appPathRoot -Name "Path" -Value $scriptDir -PropertyType String -Force | Out-Null

$capabilities = "$appRoot\Capabilities"
Ensure-Key $capabilities
New-ItemProperty -LiteralPath $capabilities -Name "ApplicationName" -Value "OMG Player" -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $capabilities -Name "ApplicationDescription" -Value "Play audio and video files with OMG Player." -PropertyType String -Force | Out-Null

Set-DefaultValue "HKCU:\Software\Classes\$progId" "OMG Player media"
Set-DefaultValue "HKCU:\Software\Classes\$progId\shell\open\command" $command

Ensure-Key "HKCU:\Software\RegisteredApplications"
New-ItemProperty -LiteralPath "HKCU:\Software\RegisteredApplications" -Name "OMG Player" -Value "Software\Classes\Applications\$appExeName\Capabilities" -PropertyType String -Force | Out-Null

Ensure-Key "$appRoot\SupportedTypes"
Ensure-Key "$capabilities\FileAssociations"

foreach ($ext in $extensions) {
    Ensure-Key "HKCU:\Software\Classes\$ext\OpenWithList\$appExeName"
    Ensure-Key "HKCU:\Software\Classes\$ext\OpenWithProgids"
    New-ItemProperty -LiteralPath "HKCU:\Software\Classes\$ext\OpenWithProgids" -Name $progId -Value ([byte[]]@()) -PropertyType Binary -Force | Out-Null
    New-ItemProperty -LiteralPath "$appRoot\SupportedTypes" -Name $ext -Value "" -PropertyType String -Force | Out-Null
    New-ItemProperty -LiteralPath "$capabilities\FileAssociations" -Name $ext -Value $progId -PropertyType String -Force | Out-Null

    $explorerOpenWith = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$ext\OpenWithList"
    Ensure-Key $explorerOpenWith
    $props = Get-ItemProperty -LiteralPath $explorerOpenWith -ErrorAction SilentlyContinue
    $existingName = $null
    foreach ($letter in 97..122) {
        $name = [string][char]$letter
        if ($props -and $props.PSObject.Properties.Name -contains $name -and $props.$name -eq $appExeName) {
            $existingName = $name
            break
        }
    }
    if (-not $existingName) {
        foreach ($letter in 97..122) {
            $name = [string][char]$letter
            if (-not ($props -and $props.PSObject.Properties.Name -contains $name)) {
                New-ItemProperty -LiteralPath $explorerOpenWith -Name $name -Value $appExeName -PropertyType String -Force | Out-Null
                $existingName = $name
                break
            }
        }
    }
    if ($existingName) {
        $mru = ""
        if ($props -and $props.PSObject.Properties.Name -contains "MRUList") {
            $mru = [string]$props.MRUList
        }
        $mru = $existingName + (($mru.ToCharArray() | Where-Object { $_ -ne $existingName }) -join "")
        New-ItemProperty -LiteralPath $explorerOpenWith -Name "MRUList" -Value $mru -PropertyType String -Force | Out-Null
    }
}

try {
    Add-Type -Namespace Win32 -Name ShellNotify -MemberDefinition @"
        [System.Runtime.InteropServices.DllImport("shell32.dll")]
        public static extern void SHChangeNotify(int wEventId, uint uFlags, System.IntPtr dwItem1, System.IntPtr dwItem2);
"@
    [Win32.ShellNotify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
} catch {
    Start-Process -FilePath "$env:windir\System32\ie4uinit.exe" -ArgumentList "-show" -WindowStyle Hidden -ErrorAction SilentlyContinue
}

Write-Host "OMG Player is now registered as an optional Open With app for common audio/video files."
