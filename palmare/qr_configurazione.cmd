@echo off
rem Doppio clic: crea il QR di configurazione dei palmari (chiede server, chiave pubblica e token).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0qr_configurazione.ps1" %*
pause
