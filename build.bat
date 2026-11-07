@echo off
rem Builds XINPUT1_4.dll (the mod). "build.bat test" also builds and runs the crash test.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out

cl /nologo /O2 /W4 /MT /LD paintfix.c /Fo:out\ /Fe:out\XINPUT1_4.dll /link user32.lib || exit /b 1

if /i "%1"=="test" (
    cl /nologo /O2 /W4 /MT test.c /Fo:out\ /Fe:out\test.exe /link user32.lib || exit /b 1
    out\test.exe || exit /b 1
)
