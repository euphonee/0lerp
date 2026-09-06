@echo off
REM Builds 0lerp.dll then the self-contained 0lerp.exe.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"

cl /nologo /LD /O2 /MT /EHsc /GS- 0lerp.cpp /link psapi.lib /OUT:0lerp.dll || exit /b 1
rc /nologo injector.rc || exit /b 1
cl /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE injector.cpp injector.res /link user32.lib /OUT:0lerp.exe || exit /b 1

echo Built 0lerp.dll and 0lerp.exe
