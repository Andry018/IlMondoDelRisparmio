@echo off
rem Prova del registratore Custom vero, da fare col tecnico in MODALITA' ADDESTRAMENTO.
rem Funziona sia dalla cartella estratta dallo zip (programmi in "app") sia da quella installata.
rem Ogni scambio col registratore finisce in Documenti\prova-rt (da mandare a chi segue il programma).
setlocal
chcp 65001 >nul
cd /d "%~dp0"
set "EXE=mdr-prova-rt.exe"
if exist "app\mdr-prova-rt.exe" set "EXE=app\mdr-prova-rt.exe"
if not exist "%EXE%" (
    echo ERRORE: mdr-prova-rt.exe non trovato accanto a questo file.
    goto fine
)

echo MONDO DEL RISPARMIO - PROVA DEL REGISTRATORE
echo.
echo Primo passo: solo letture, non esce nessun documento.
echo.
set "IP="
set /p "IP=Indirizzo IP del registratore (es. 192.168.1.50): "
if "%IP%"=="" goto fine
echo.
"%EXE%" %IP%
if errorlevel 1 (
    echo.
    echo La lettura non e' riuscita: controllare indirizzo, cavo di rete e servizio XML del registratore.
    goto fine
)

echo.
echo Secondo passo: 4 vendite di prova, un reso, un annullo, ristampa e cassetto.
echo Si fa SOLO con il registratore in MODALITA' ADDESTRAMENTO: se non lo e', il programma si ferma da solo.
echo.
set "R="
set /p "R=Il registratore e' in addestramento e vuoi emettere i documenti di prova? (S/N): "
if /i not "%R%"=="S" goto fine
set "Z="
set /p "Z=Fare anche la chiusura Z di prova alla fine? (S/N): "
echo.
if /i "%Z%"=="S" (
    "%EXE%" %IP% --addestramento --chiusura
) else (
    "%EXE%" %IP% --addestramento
)

:fine
echo.
echo Il registro della prova e' in Documenti\prova-rt
echo Tenere anche le stampe uscite dal registratore (una foto va bene).
echo.
pause
