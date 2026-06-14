@echo off
setlocal

set PROJECT_DIR=%~dp0

:: Find vcvarsall.bat
set VCVARS_ALL=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat
if exist "%VCVARS_ALL%" goto :found_vcvars

set VCVARS_ALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
if exist "%VCVARS_ALL%" goto :found_vcvars

echo ERROR: Could not find vcvarsall.bat. Make sure Visual Studio is installed.
pause
exit /b 1

:found_vcvars
call "%VCVARS_ALL%" x64
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to set up MSVC environment.
    pause
    exit /b 1
)

cd /d "%PROJECT_DIR%"

:: Configure with CMake if not already done
if not exist "build\build.ninja" (
    echo Configuring build with CMake...
    cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release .
    if %ERRORLEVEL% neq 0 (
        echo ERROR: CMake configure failed.
        pause
        exit /b 1
    )
)

:: Build with Ninja
echo Building...
ninja -C build
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)

echo.
echo Build successful! Binary: build\ghost-mirror.exe
pause
exit /b 0
