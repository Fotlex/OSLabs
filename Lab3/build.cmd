@echo off
setlocal enabledelayedexpansion

REM
cd /d "%~dp0"

set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"
set "BUILD_DIR=build"

echo [INFO] Project directory: %CD%
echo [INFO] Build type: %BUILD_TYPE%

REM
echo [INFO] Updating source code from Git repository...
git pull
if errorlevel 1 (
    echo [ERROR] git pull failed!
    exit /b 1
)


REM
set "CMAKE_GEN="
where mingw32-make >nul 2>&1
if not errorlevel 1 (
    set "CMAKE_GEN=-G "MinGW Makefiles""
) else (
    where ninja >nul 2>&1
    if not errorlevel 1 (
        set "CMAKE_GEN=-G "Ninja""
    )
)

REM
echo [INFO] Configuring project with CMake...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"

cmake %CMAKE_GEN% -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ..
if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    popd
    exit /b 1
)

REM
echo [INFO] Building project...
cmake --build . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Build failed!
    popd
    exit /b 1
)

popd
echo [OK] Build completed successfully.
endlocal