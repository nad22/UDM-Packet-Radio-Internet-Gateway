#!/usr/bin/env python3
"""
Einfaches OTA Copy-Script für UDM-PRIG
======================================

Kopiert die kompilierte firmware.bin automatisch nach ota/udm-prig-client.ino.bin
Überschreibt die Datei jedes Mal - Version wird separat verwaltet.
"""

import os
import shutil

Import("env")

def copy_firmware_to_ota(source, target, env):
    """
    Post-build hook - kopiert firmware.bin ins OTA-Verzeichnis
    """
    # Pfade ermitteln - source[0] sollte firmware.bin sein, aber wir müssen sicherstellen dass es .bin ist
    source_path = str(source[0])
    project_dir = env.get("PROJECT_DIR")
    
    # Stelle sicher, dass wir die .bin Datei und nicht die .elf haben
    if source_path.endswith('.elf'):
        firmware_path = source_path.replace('.elf', '.bin')
    else:
        firmware_path = source_path
    
    # OTA-Ziel
    ota_dir = os.path.join(project_dir, "ota")
    ota_firmware = os.path.join(ota_dir, "udm-prig-client.ino.bin")
    
    # Debug-Ausgabe
    print(f"🔍 Firmware gefunden: {firmware_path}")
    
    # Prüfen ob firmware.bin existiert
    if os.path.exists(firmware_path):
        # OTA-Verzeichnis erstellen falls nötig
        if not os.path.exists(ota_dir):
            os.makedirs(ota_dir)
            
        # Firmware kopieren (überschreibt automatisch)
        shutil.copy2(firmware_path, ota_firmware)
        
        # Dateigröße ermitteln
        size = os.path.getsize(ota_firmware)
        size_kb = size / 1024
        
        print(f"✅ OTA-Firmware erstellt: {ota_firmware}")
        print(f"📦 Größe: {size:,} bytes ({size_kb:.1f} KB)")
    else:
        print(f"❌ Firmware nicht gefunden: {firmware_path}")

# Hook registrieren - wird nach der Erstellung der firmware.bin ausgeführt
env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware_to_ota)