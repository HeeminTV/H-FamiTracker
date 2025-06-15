:: Written by HeeminYT to make it easier to convert png to ico
@echo off
png2ico.exe "%~n1.ico" --colors 16 "%~n1.png"
exit /b