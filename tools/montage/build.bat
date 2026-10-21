@echo off
rem Builds montage.exe with the MSVC build tools (x86, like the game)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /EHsc /W3 /DUNICODE /D_UNICODE montage.cpp /Fe:montage.exe
