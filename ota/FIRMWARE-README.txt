# Placeholder für UDMPRG-Client.ino.bin

Diese Datei sollte durch die echte kompilierte ESP32-Firmware ersetzt werden.

## So erstellen Sie die .bin-Datei:

1. **Arduino IDE öffnen**
2. **udm-prig-client.ino** laden
3. **Sketch -> Export compiled Binary** wählen
4. **Die erstellte .bin-Datei hierher kopieren**
5. **Version in version.txt erhöhen**
6. **Committen und pushen**

## GitHub Raw URL anpassen

In der ino-Datei die URL anpassen:
```cpp
String otaRepoUrl = "https://raw.githubusercontent.com/IHR-USERNAME/IHR-REPO/main/ota";
```