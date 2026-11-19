@echo off
SET DLL=%~dp0bin\x64\Release\MusicLounge.dll
SET CLSID={C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}

if not exist "%DLL%" (
    echo ERREUR : DLL introuvable : %DLL%
    echo Compilez d'abord en Release x64
    pause & exit /b 1
)

echo Ajout des cles de registre...

reg add "HKLM\Software\Classes\CLSID\%CLSID%" /ve /d "Music Lounge" /f
if %errorlevel% neq 0 ( echo ECHEC sur cle principale & pause & exit /b 1 )

reg add "HKLM\Software\Classes\CLSID\%CLSID%\InprocServer32" /ve /d "%DLL%" /f
if %errorlevel% neq 0 ( echo ECHEC sur InprocServer32 & pause & exit /b 1 )

reg add "HKLM\Software\Classes\CLSID\%CLSID%\InprocServer32" /v "ThreadingModel" /d "Apartment" /f
if %errorlevel% neq 0 ( echo ECHEC sur ThreadingModel & pause & exit /b 1 )

reg add "HKLM\Software\Classes\CLSID\%CLSID%\Implemented Categories\{00021492-0000-0000-C000-000000000046}" /ve /d "" /f
if %errorlevel% neq 0 ( echo ECHEC sur Implemented Categories & pause & exit /b 1 )

echo.
echo Enregistrement OK. Redemarrage d'Explorer...
taskkill /f /im explorer.exe
start explorer.exe
echo.
echo Clic droit taskbar -^> Barres d'outils -^> Music Lounge
pause
