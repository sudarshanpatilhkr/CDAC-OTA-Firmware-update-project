#!/usr/bin/env python3
"""
upload_firmware.py - Upload firmware binary to OTA server

Usage:
    python scripts/upload_firmware.py <firmware.bin> <version>
    
Example:
    python scripts/upload_firmware.py build/firmware.bin 1.2.0
"""

import sys
import os
import hashlib
import requests

SERVER_URL = os.environ.get('OTA_SERVER_URL', 'http://localhost:5000')


def upload(filepath, version):
    """Upload firmware to OTA server."""
    
    if not os.path.exists(filepath):
        print(f"❌ File not found: {filepath}")
        sys.exit(1)
    
    filesize = os.path.getsize(filepath)
    
    # Compute SHA-256
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        while True:
            block = f.read(4096)
            if not block:
                break
            sha256.update(block)
    
    print(f"📦 Uploading firmware:")
    print(f"   File: {filepath}")
    print(f"   Version: {version}")
    print(f"   Size: {filesize} bytes")
    print(f"   SHA-256: {sha256.hexdigest()[:32]}...")
    print()
    
    # Upload
    with open(filepath, 'rb') as f:
        resp = requests.post(
            f"{SERVER_URL}/api/firmware/upload",
            files={'file': (os.path.basename(filepath), f)},
            data={'version': version}
        )
    
    if resp.status_code == 200:
        data = resp.json()
        print(f"✅ Upload successful!")
        print(f"   Server SHA-256: {data['sha256'][:32]}...")
        print(f"   Pages: {data.get('pages', 'N/A')}")
    else:
        print(f"❌ Upload failed: {resp.status_code}")
        print(f"   {resp.text}")
        sys.exit(1)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python upload_firmware.py <firmware.bin> <version>")
        print("Example: python upload_firmware.py build/firmware.bin 1.2.0")
        sys.exit(1)
    
    upload(sys.argv[1], sys.argv[2])
