# OMG Player

OMG Player is a small native Windows media player for the OMG Tools collection.

## Features

- Play common video and audio files through Windows Media Foundation.
- Open files from the toolbar or drag a media file into the window.
- Open a media file directly with `OMG Player.exe "file.mp4"`.
- Play, pause, stop, seek, mute, and adjust volume.
- Flat borderless emoji controls with a clipped ripple effect on click.
- Flat custom progress and volume sliders.
- Uses `favicon.ico` as the app icon.
- High-DPI aware rendering for sharper text and controls.
- Audio files open in a compact player without a video surface.
- Video files show the video surface above the bottom playback bar.
- Shows current time, total duration, and playback status.
- Keyboard shortcuts:
  - `Ctrl+O`: open media
  - `Space` / `P`: play or pause
  - `Left`: seek back 15 seconds
  - `Right`: seek forward 30 seconds
  - `Ctrl+Left` / `Ctrl+Right`: previous or next media file in the same folder
  - `Up` / `Down`: volume up or down
  - Double-click video area: maximize or restore window

## Build

Run this from PowerShell:

```powershell
.\build.ps1
```

The output is:

```text
OMG Player.exe
```

This project uses native Win32 APIs, Media Foundation, and MinGW-w64 `g++`; no Python runtime is required.

## Register in Windows Open With

Run:

```powershell
.\Register-OpenWith.ps1
```

OMG Player will appear as an optional app in Windows "Open with" for common audio and video files. It does not change your current default player.

To remove the registration:

```powershell
.\Unregister-OpenWith.ps1
```
