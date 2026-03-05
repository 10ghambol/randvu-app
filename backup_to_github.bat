@echo off
echo ==============================================
echo Randvu Project Auto-Backup to GitHub
echo ==============================================
echo.

cd /d "%~dp0"

echo [1/3] Adding all updated files...
git add .
if %errorlevel% neq 0 (
    echo [ERROR] Git add failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [2/3] Committing changes...
:: Use the current date and time for the commit message
for /f "tokens=1-4 delims=/ " %%i in ("%date%") do set d=%%l-%%j-%%k
for /f "tokens=1-3 delims=:." %%i in ("%time%") do set t=%%i:%%j
git commit -m "Auto-backup on %date% %time%"
:: It's okay if commit fails (e.g., if there are no changes to commit)

echo.
echo [3/3] Pushing to GitHub...
git push
if %errorlevel% neq 0 (
    echo [ERROR] Git push failed! Please check your internet connection or GitHub credentials.
    pause
    exit /b %errorlevel%
)

echo.
echo ==============================================
echo [SUCCESS] Project successfully backed up to GitHub!
echo ==============================================
echo.
timeout /t 5 >nul
