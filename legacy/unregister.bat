@echo off
set DLL=%~dp0bin\x64\Release\MusicLounge.dll
if not exist "%DLL%" (
    echo DLL not found - trying to clean registry directly...
    reg delete "HKCU\Software\Classes\CLSID\{C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}" /f
    reg delete "HKLM\Software\Classes\CLSID\{C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}" /f 2>nul
    goto done
)
regsvr32 /u "%DLL%"
:done
echo Done. Explorer may need a restart to complete removal.
pause
