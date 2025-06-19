@ECHO OFF
SETLOCAL ENABLEDELAYEDEXPANSION
FOR /R %%A IN (png\*.png) DO IF EXIST %%A (
	png2ico.exe "..\%%~nA.ico" --colors 16 "%%A"
)
EXIT /B