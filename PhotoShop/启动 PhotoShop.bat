@echo off
setlocal
set "APP=%~dp0src-tauri\target\release\photoshop-studio.exe"

if not exist "%APP%" (
  echo PhotoShop 尚未构建。请先运行 README 中的构建命令。
  pause
  exit /b 1
)

start "PhotoShop" "%APP%"
