@echo off
cd /d "%~dp0"
py -3 oi_rank_scraper.py 2>nul
if %errorlevel% equ 0 exit /b 0

python oi_rank_scraper.py 2>nul
if %errorlevel% equ 0 exit /b 0

start "" "%~dp0index.html"
