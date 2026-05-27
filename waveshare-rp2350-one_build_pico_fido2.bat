@echo off
setlocal EnableDelayedExpansion

:: =================配置区域=================
set "VERSION_MAJOR=6"
set "VERSION_MINOR=6"
set "SUFFIX=%VERSION_MAJOR%.%VERSION_MINOR%"

:: 目标开发板名称
set "TARGET_BOARD=waveshare_rp2350_one"

:: 默认 PICO_SDK_PATH
if not defined PICO_SDK_PATH (
    set "PICO_SDK_PATH=..\..\pico-sdk"
)

:: 默认密钥路径
if not defined SECURE_BOOT_PKEY (
    set "SECURE_BOOT_PKEY=..\ec_private_key.pem"
)

:: =========================================

echo [INFO] Starting build process for board: %TARGET_BOARD% (Version %SUFFIX%) using Ninja...

:: 创建目录
if not exist "build_release" mkdir build_release
if not exist "release" mkdir release

:: 进入构建目录
cd /d build_release

:: 清理当前构建目录
echo [INFO] Cleaning build directory...
del /q /f /a *.* 2>nul
for /d %%D in (*) do rd /s /q "%%D" 2>nul

:: 配置 CMake
echo [CMAKE] Configuring with Ninja for %TARGET_BOARD%...

:: 【尝试方案】
:: 1. 使用 MinSizeRel 优化大小
:: 2. 显式禁用 Copy to RAM (虽然可能被子项目覆盖)
:: 3. 如果依然失败，可能需要在此处添加 -DENABLE_OATH=0 等来削减功能
cmake .. ^
    -G Ninja ^
    -DPICO_SDK_PATH="%PICO_SDK_PATH%" ^
    -DPICO_BOARD=%TARGET_BOARD% ^
    -DSECURE_BOOT_PKEY="%SECURE_BOOT_PKEY%" ^
    -DENABLE_EDDSA=1 ^
    -DCMAKE_BUILD_TYPE=MinSizeRel ^
    -DPICO_COPY_TO_RAM=0

if errorlevel 1 (
    echo [ERROR] CMake configuration failed for %TARGET_BOARD%
    cd /d ..
    pause
    exit /b 1
)

:: 编译
echo [NINJA] Compiling...
ninja -j %NUMBER_OF_PROCESSORS%

if errorlevel 1 (
    echo [ERROR] Build failed for %TARGET_BOARD%
    echo [HINT] RAM Overflow. Try disabling features like OATH/OTP in CMakeLists.txt or check board header RAM size.
    cd /d ..
    pause
    exit /b 1
)

:: 移动文件
if exist "pico_fido2.uf2" (
    move /y "pico_fido2.uf2" "..\release\pico_fido2_%TARGET_BOARD%-%SUFFIX%.uf2"
    echo [SUCCESS] Created: ..\release\pico_fido2_%TARGET_BOARD%-%SUFFIX%.uf2
) else (
    echo [ERROR] pico_fido2.uf2 not found for %TARGET_BOARD%
    cd /d ..
    pause
    exit /b 1
)

cd /d ..
echo.
echo [INFO] Build completed successfully.
if not "%1"=="--no-pause" pause
endlocal