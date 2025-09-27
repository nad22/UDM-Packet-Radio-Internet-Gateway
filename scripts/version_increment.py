#!/usr/bin/env python3
"""
Version increment script for UDM-PRIG PlatformIO build
Automatically increments the build version for each compilation
"""

import os
import re
from datetime import datetime

def increment_version():
    # Define paths
    src_main = "src/main.cpp"
    version_file = "version.txt"
    
    # Read current version from main.cpp
    if os.path.exists(src_main):
        with open(src_main, 'r', encoding='utf-8') as f:
            content = f.read()
            
        # Search for version string
        version_match = re.search(r'char localVersion\[16\] = "([^"]+)";', content)
        if version_match:
            current_version = version_match.group(1)
            print(f"Current version: {current_version}")
            
            # Parse version (e.g., "3.0.0")
            parts = current_version.split('.')
            if len(parts) == 3:
                major, minor, patch = int(parts[0]), int(parts[1]), int(parts[2])
                
                # Increment patch version
                patch += 1
                new_version = f"{major}.{minor}.{patch}"
                
                # Update version in source file
                new_content = re.sub(
                    r'char localVersion\[16\] = "[^"]+";',
                    f'char localVersion[16] = "{new_version}";',
                    content
                )
                
                with open(src_main, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                
                # Write version to version.txt for OTA
                with open(version_file, 'w') as f:
                    f.write(new_version)
                
                print(f"Version updated to: {new_version}")
                
                # Add build timestamp comment
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                print(f"Build timestamp: {timestamp}")
            else:
                print(f"Invalid version format: {current_version}")
        else:
            print("Version string not found in main.cpp")
    else:
        print(f"Source file not found: {src_main}")

if __name__ == "__main__":
    increment_version()