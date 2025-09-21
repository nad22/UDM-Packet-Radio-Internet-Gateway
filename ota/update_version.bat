@echo off
REM Batch-Skript zum Aktualisieren der OTA-Version
REM Verwendung: update_version.bat [neue_version]

if "%1"=="" (
    echo Verwendung: update_version.bat [neue_version]
    echo Beispiel: update_version.bat 1.0.2
    pause
    exit /b 1
)

set NEW_VERSION=%1

echo Aktualisiere Version auf %NEW_VERSION%...

REM Version in version.txt aktualisieren
echo %NEW_VERSION% > ota\version.txt

REM Version im ESP32-Code aktualisieren
powershell -Command "(gc 'ino\udm-prig-client.ino') -replace 'const String localVersion = \"[^\"]*\";', 'const String localVersion = \"%NEW_VERSION%\";' | sc 'ino\udm-prig-client.ino'"

echo Version erfolgreich auf %NEW_VERSION% aktualisiert!
echo.
echo Nächste Schritte:
echo 1. Kompilieren Sie die Firmware in Arduino IDE
echo 2. Exportieren Sie die kompilierte Binary (Sketch -> Export compiled Binary)
echo 3. Kopieren Sie die .bin Datei nach ota\udm-prig-client.ino.esp32.bin
echo 4. Committen und pushen Sie alle Änderungen zu GitHub
echo.

pause