@echo off
setlocal
cd /d "%~dp0"

where python >nul 2>nul
if %errorlevel%==0 goto :serve
where py >nul 2>nul
if %errorlevel%==0 goto :serve_py

echo Python was not found. Opening the app directly.
echo If the Connect button stays disabled, install Python 3 and run this file again.
start "" "index.html"
goto :eof

:serve
start "" /min cmd /c "python -m http.server 8321"
goto :open

:serve_py
start "" /min cmd /c "py -m http.server 8321"
goto :open

:open
timeout /t 1 /nobreak >nul
start "" "http://localhost:8321/index.html"
goto :eof
