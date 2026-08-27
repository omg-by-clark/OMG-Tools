$ErrorActionPreference = "Stop"

$appExeName = "OMG Player.exe"
$progId = "OMGPlayer.media"
$extensions = @(
    ".mp4", ".m4v", ".mov", ".mkv", ".avi", ".wmv", ".mpg", ".mpeg",
    ".mp3", ".wav", ".wma", ".aac", ".m4a", ".flac", ".ogg"
)

foreach ($ext in $extensions) {
    $openWith = "HKCU:\Software\Classes\$ext\OpenWithProgids"
    if (Test-Path -LiteralPath $openWith) {
        Remove-ItemProperty -LiteralPath $openWith -Name $progId -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath "HKCU:\Software\Classes\$ext\OpenWithList\$appExeName" -Recurse -Force -ErrorAction SilentlyContinue

    $explorerOpenWith = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$ext\OpenWithList"
    if (Test-Path -LiteralPath $explorerOpenWith) {
        $props = Get-ItemProperty -LiteralPath $explorerOpenWith -ErrorAction SilentlyContinue
        foreach ($letter in 97..122) {
            $name = [string][char]$letter
            if ($props -and $props.PSObject.Properties.Name -contains $name -and $props.$name -eq $appExeName) {
                Remove-ItemProperty -LiteralPath $explorerOpenWith -Name $name -ErrorAction SilentlyContinue
            }
        }
        $props = Get-ItemProperty -LiteralPath $explorerOpenWith -ErrorAction SilentlyContinue
        $letters = @()
        foreach ($letter in 97..122) {
            $name = [string][char]$letter
            if ($props -and $props.PSObject.Properties.Name -contains $name) {
                $letters += $name
            }
        }
        if ($letters.Count -gt 0) {
            New-ItemProperty -LiteralPath $explorerOpenWith -Name "MRUList" -Value ($letters -join "") -PropertyType String -Force | Out-Null
        } else {
            Remove-ItemProperty -LiteralPath $explorerOpenWith -Name "MRUList" -ErrorAction SilentlyContinue
        }
    }
}

Remove-ItemProperty -LiteralPath "HKCU:\Software\RegisteredApplications" -Name "OMG Player" -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "HKCU:\Software\Classes\Applications\$appExeName" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "HKCU:\Software\Classes\$progId" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\$appExeName" -Recurse -Force -ErrorAction SilentlyContinue

try {
    Add-Type -Namespace Win32 -Name ShellNotify -MemberDefinition @"
        [System.Runtime.InteropServices.DllImport("shell32.dll")]
        public static extern void SHChangeNotify(int wEventId, uint uFlags, System.IntPtr dwItem1, System.IntPtr dwItem2);
"@
    [Win32.ShellNotify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
} catch {
    Start-Process -FilePath "$env:windir\System32\ie4uinit.exe" -ArgumentList "-show" -WindowStyle Hidden -ErrorAction SilentlyContinue
}

Write-Host "OMG Player Open With registration was removed."
