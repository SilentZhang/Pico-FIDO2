@echo off
setlocal EnableDelayedExpansion

:: =================配置区域=================
set "VERSION_MAJOR=6"
set "VERSION_MINOR=6"
set "SUFFIX=%VERSION_MAJOR%.%VERSION_MINOR%"

:: 如果需要在 GitHub Actions 中使用 SHA，可以解开下面这行
:: if defined GITHUB_SHA set "SUFFIX=%SUFFIX%.%GITHUB_SHA%"

:: 默认 PICO_SDK_PATH，如果环境变量未设置则使用相对路径
if not defined PICO_SDK_PATH (
    set "PICO_SDK_PATH=..\..\pico-sdk"
)

:: 默认密钥路径
if not defined SECURE_BOOT_PKEY (
    set "SECURE_BOOT_PKEY=..\..\ec_private_key.pem"
)

:: =========================================

echo [INFO] Starting build process for version %SUFFIX% using Ninja...

:: 创建目录
if not exist "build_release" mkdir build_release
if not exist "release" mkdir release

:: 清空 release 目录 (相当于 rm -rf release/*)
echo [INFO] Cleaning release directory...
del /q /f "release\*" 2>nul

:: 进入构建目录
cd /d build_release

:: 获取 boards 目录路径
set "BOARD_DIR=%PICO_SDK_PATH%\src\boards\include\boards"

:: 检查 board 目录是否存在
if not exist "%BOARD_DIR%" (
    echo [ERROR] Board directory not found: %BOARD_DIR%
    echo [ERROR] Please check PICO_SDK_PATH: %PICO_SDK_PATH%
    pause
    exit /b 1
)

:: 检查 ninja 是否在 PATH 中
where ninja >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Ninja is not found in PATH. Please install Ninja and add it to your system PATH.
    pause
    exit /b 1
)

echo [INFO] Scanning boards in: %BOARD_DIR%

:: 遍历所有 .h 文件
for %%F in ("%BOARD_DIR%\*.h") do (
    :: 获取文件名（不含扩展名）
    set "board_name=%%~nF"
    
    echo.
    echo [BUILD] Building for board: !board_name!
    echo --------------------------------------------------

    :: 【修改点1】清理当前构建目录下的所有文件，包括隐藏文件 (.ninja_deps 等)
    :: /a 属性确保删除隐藏文件和系统文件
    del /q /f /a *.* 2>nul
    for /d %%D in (*) do rd /s /q "%%D" 2>nul

    :: 配置 CMake，指定使用 Ninja 生成器
    echo [CMAKE] Configuring with Ninja...
    
    :: 【修改点2】显式传递 PICO_SDK_PATH 给 CMake，防止环境变量传递失效
    cmake .. ^
        -G Ninja ^
        -DPICO_SDK_PATH="%PICO_SDK_PATH%" ^
        -DPICO_BOARD=!board_name! ^
        -DSECURE_BOOT_PKEY="%SECURE_BOOT_PKEY%" ^
        -DENABLE_EDDSA=1

    if errorlevel 1 (
        echo [ERROR] CMake configuration failed for !board_name!
        :: 【修改点3】错误退出前返回根目录，方便用户重新运行
        cd /d ..
        pause
        exit /b 1
    )

    :: 编译使用 Ninja
    echo [NINJA] Compiling...
    ninja -j %NUMBER_OF_PROCESSORS%

    if errorlevel 1 (
        echo [ERROR] Build failed for !board_name!
        cd /d ..
        pause
        exit /b 1
    )

    :: 移动生成的 uf2 文件到 release 目录
    if exist "pico_fido2.uf2" (
        move /y "pico_fido2.uf2" "..\release\pico_fido2_!board_name!-%SUFFIX%.uf2"
        echo [SUCCESS] Created: ..\release\pico_fido2_!board_name!-%SUFFIX%.uf2
    ) else (
        echo [WARNING] pico_fido2.uf2 not found for !board_name!
    )
)

:: 返回根目录
cd /d ..

echo.
echo [INFO] All builds completed. Check the 'release' folder.
:: 【修改点4】如果是自动化运行，建议移除 pause，或者根据参数决定
if not "%1"=="--no-pause" pause
endlocal