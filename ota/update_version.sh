#!/bin/bash
# Shell-Skript zum Aktualisieren der OTA-Version
# Verwendung: ./update_version.sh [neue_version]

if [ -z "$1" ]; then
    echo "Verwendung: ./update_version.sh [neue_version]"
    echo "Beispiel: ./update_version.sh 1.0.2"
    exit 1
fi

NEW_VERSION=$1

echo "Aktualisiere Version auf $NEW_VERSION..."

# Version in version.txt aktualisieren
echo "$NEW_VERSION" > ota/version.txt

# Version im ESP32-Code aktualisieren
sed -i "s/const String localVersion = \"[^\"]*\";/const String localVersion = \"$NEW_VERSION\";/g" ino/udm-prig-client.ino

echo "Version erfolgreich auf $NEW_VERSION aktualisiert!"
echo ""
echo "Nächste Schritte:"
echo "1. Kompilieren Sie die Firmware in Arduino IDE"
echo "2. Exportieren Sie die kompilierte Binary (Sketch -> Export compiled Binary)"
echo "3. Kopieren Sie die .bin Datei nach ota/udm-prig-client.ino.esp32.bin"
echo "4. Committen und pushen Sie alle Änderungen zu GitHub"
echo ""