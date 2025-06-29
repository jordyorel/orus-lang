@echo off
REM Orus Playground Launcher for Windows
setlocal enabledelayedexpansion

echo.
echo ^🚀 Orus Playground Launcher
echo =========================================

REM Get script directory and project root
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "PLAYGROUND_DIR=%SCRIPT_DIR%.."

REM Check if Orus binary exists
set "ORUS_BINARY=%PROJECT_ROOT%\orus.exe"
if not exist "%ORUS_BINARY%" (
    set "ORUS_BINARY=%PROJECT_ROOT%\orus"
    if not exist "!ORUS_BINARY!" (
        echo ^⚠️  Orus binary not found
        echo    Building Orus first...
        
        cd /d "%PROJECT_ROOT%"
        make
        if errorlevel 1 (
            echo ^❌ Failed to build Orus
            pause
            exit /b 1
        )
        
        if not exist "%ORUS_BINARY%" if not exist "!ORUS_BINARY!" (
            echo ^❌ Orus binary still not found after build
            pause
            exit /b 1
        )
        
        echo ^✅ Orus built successfully
    )
)

REM Check for Python
set "PYTHON_CMD="
python --version >nul 2>&1
if %errorlevel% equ 0 (
    for /f "tokens=2" %%i in ('python --version 2^>^&1') do (
        for /f "tokens=1 delims=." %%j in ("%%i") do (
            if "%%j"=="3" set "PYTHON_CMD=python"
        )
    )
)

if "%PYTHON_CMD%"=="" (
    python3 --version >nul 2>&1
    if %errorlevel% equ 0 (
        set "PYTHON_CMD=python3"
    )
)

if "%PYTHON_CMD%"=="" (
    echo ^❌ Python 3 is required but not found
    echo    Please install Python 3 and try again
    pause
    exit /b 1
)

echo ^✅ Using Python: %PYTHON_CMD%
echo ^✅ Orus binary found

REM Default port
set "PORT=%1"
if "%PORT%"=="" set "PORT=8000"

REM Start the server
echo.
echo ^🌐 Starting playground server on port %PORT%...
echo    Open your browser to: http://localhost:%PORT%
echo    Press Ctrl+C to stop the server
echo.

cd /d "%PLAYGROUND_DIR%"
"%PYTHON_CMD%" "%SCRIPT_DIR%\server.py" %PORT%