@echo off
rem Lo script viene copiato in TEMP: la cartella del programma (che contiene questo file) va cancellata.
copy /y "%~dp0disinstalla.ps1" "%TEMP%\mdr_disinstalla.ps1" >nul
rem Fuori dalla cartella del programma: Windows non lascia cancellare la cartella corrente
cd /d "%TEMP%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%TEMP%\mdr_disinstalla.ps1" & pause & exit /b
