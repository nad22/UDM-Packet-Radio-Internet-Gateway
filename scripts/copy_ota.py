#!/usr/bin/env python3
"""
OTA copy script for UDM-PRIG
===========================

Copies the compiled firmware.bin to ota/udm-prig-client.ino.bin.
The file is overwritten each time; the version is managed separately.
"""

import os
import shutil

Import("env")

def copy_firmware_to_ota(source, target, env):
    """
    Post-build hook: copy firmware.bin to the OTA directory.
    """
    # Resolve the firmware binary path.
    source_path = str(source[0])
    project_dir = env.get("PROJECT_DIR")
    
    # Use the .bin file rather than the .elf file.
    if source_path.endswith('.elf'):
        firmware_path = source_path.replace('.elf', '.bin')
    else:
        firmware_path = source_path
    
    # OTA destination
    ota_dir = os.path.join(project_dir, "ota")
    ota_firmware = os.path.join(ota_dir, "udm-prig-client.ino.bin")
    
    # Debug output
    print(f"🔍 Firmware found: {firmware_path}")
    
    # Check that firmware.bin exists.
    if os.path.exists(firmware_path):
        # Create the OTA directory if necessary.
        if not os.path.exists(ota_dir):
            os.makedirs(ota_dir)
            
        # Copy firmware (overwrites the destination).
        shutil.copy2(firmware_path, ota_firmware)
        
        # Get the file size.
        size = os.path.getsize(ota_firmware)
        size_kb = size / 1024
        
        print(f"✅ OTA firmware created: {ota_firmware}")
        print(f"📦 Size: {size:,} bytes ({size_kb:.1f} KB)")
    else:
        print(f"❌ Firmware not found: {firmware_path}")

# Register the hook after firmware.bin is generated.
env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware_to_ota)