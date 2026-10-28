@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0"

echo ==================================================
echo   老干部活动中心 打包（生成可以直接发给别人的压缩包）
echo ==================================================
echo.

if not exist "LianqinHost.exe" (
    echo [错误] 还没有 LianqinHost.exe，请先运行 build.bat 编译。
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1"
if errorlevel 1 (
    echo.
    echo 打包没有完成，请把上面的错误信息发给我。
    pause
    exit /b 1
)

echo.
echo ==================================================
echo   打包完成！压缩包在“发布”文件夹里。
echo ==================================================
pause
exit /b 0
