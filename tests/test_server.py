#!/usr/bin/env python3
"""
test_server.py - Test script for OTA Server
CDAC ACTS PG-Diploma in DESD

Tests all server endpoints and delta patch generation.
Run with: python tests/test_server.py
"""

import os
import sys
import hashlib
import struct
import requests
import tempfile

SERVER_URL = "http://localhost:5000"
PAGE_SIZE = 1024


def create_test_firmware(size=10240, seed=42):
    """Create a deterministic test firmware binary."""
    import random
    random.seed(seed)
    data = bytes([random.randint(0, 255) for _ in range(size)])
    return data


def test_health_check():
    """Test server health endpoint."""
    print("[TEST] Health Check...")
    resp = requests.get(f"{SERVER_URL}/api/health")
    assert resp.status_code == 200
    data = resp.json()
    assert data['status'] == 'ok'
    print(f"  ✅ Server is healthy: {data['server']}")


def test_upload_firmware():
    """Test firmware upload."""
    print("\n[TEST] Upload Firmware v1.0.0...")
    
    fw_v1 = create_test_firmware(10240, seed=100)
    
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(fw_v1)
        f.flush()
        
        with open(f.name, 'rb') as upload:
            resp = requests.post(
                f"{SERVER_URL}/api/firmware/upload",
                files={'file': ('firmware_v1.0.0.bin', upload)},
                data={'version': '1.0.0'}
            )
    
    assert resp.status_code == 200
    data = resp.json()
    print(f"  ✅ Uploaded: {data['size']} bytes, SHA256: {data['sha256'][:16]}...")
    
    # Upload v1.1.0 (slightly different)
    print("\n[TEST] Upload Firmware v1.1.0...")
    fw_v2 = bytearray(fw_v1)
    # Modify a few pages
    fw_v2[1024:2048] = bytes([0xAA] * 1024)   # Change page 1
    fw_v2[5120:6144] = bytes([0xBB] * 1024)   # Change page 5
    fw_v2 = bytes(fw_v2)
    
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(fw_v2)
        f.flush()
        
        with open(f.name, 'rb') as upload:
            resp = requests.post(
                f"{SERVER_URL}/api/firmware/upload",
                files={'file': ('firmware_v1.1.0.bin', upload)},
                data={'version': '1.1.0'}
            )
    
    assert resp.status_code == 200
    data = resp.json()
    print(f"  ✅ Uploaded: {data['size']} bytes, SHA256: {data['sha256'][:16]}...")


def test_get_version():
    """Test version endpoint."""
    print("\n[TEST] Get Latest Version...")
    resp = requests.get(f"{SERVER_URL}/api/version")
    assert resp.status_code == 200
    data = resp.json()
    print(f"  ✅ Latest version: {data['version']}")
    print(f"     SHA256: {data['sha256'][:32]}...")
    print(f"     Size: {data['size']} bytes")


def test_delta_patch_info():
    """Test delta patch info endpoint."""
    print("\n[TEST] Delta Patch Info (v1.0.0 -> v1.1.0)...")
    resp = requests.post(
        f"{SERVER_URL}/api/firmware/delta/info",
        json={'current_version': '1.0.0', 'target_version': '1.1.0'}
    )
    assert resp.status_code == 200
    data = resp.json()
    print(f"  ✅ Delta patch stats:")
    print(f"     Total pages: {data['total_pages']}")
    print(f"     Changed pages: {data['changed_pages']}")
    print(f"     Unchanged pages: {data['unchanged_pages']}")
    print(f"     Patch size: {data['patch_size']} bytes")
    print(f"     Compression: {data['compression_ratio']}")


def test_delta_patch_download():
    """Test delta patch download."""
    print("\n[TEST] Download Delta Patch...")
    resp = requests.post(
        f"{SERVER_URL}/api/firmware/delta",
        json={'current_version': '1.0.0', 'target_version': '1.1.0'}
    )
    assert resp.status_code == 200
    
    patch = resp.content
    print(f"  ✅ Patch downloaded: {len(patch)} bytes")
    
    # Verify patch header
    magic = struct.unpack('<I', patch[:4])[0]
    assert magic == 0x4F544150, f"Invalid magic: {hex(magic)}"
    
    version = struct.unpack('<H', patch[4:6])[0]
    num_changed = struct.unpack('<H', patch[6:8])[0]
    new_size = struct.unpack('<I', patch[8:12])[0]
    total_pages = struct.unpack('<H', patch[12:14])[0]
    page_size = struct.unpack('<H', patch[14:16])[0]
    
    print(f"  ✅ Patch header valid:")
    print(f"     Magic: OTAP")
    print(f"     Format version: {version}")
    print(f"     Changed pages: {num_changed}")
    print(f"     New FW size: {new_size}")
    print(f"     Total pages: {total_pages}")
    print(f"     Page size: {page_size}")
    
    # Extract SHA-256 from last 32 bytes
    sha256_hash = patch[-32:].hex()
    print(f"     New FW SHA256: {sha256_hash[:32]}...")


def test_full_firmware_download():
    """Test full firmware download."""
    print("\n[TEST] Download Full Firmware...")
    resp = requests.get(f"{SERVER_URL}/api/firmware/full")
    assert resp.status_code == 200
    
    fw = resp.content
    sha256 = hashlib.sha256(fw).hexdigest()
    print(f"  ✅ Downloaded: {len(fw)} bytes")
    print(f"     SHA256: {sha256[:32]}...")


if __name__ == '__main__':
    print("=" * 60)
    print("  OTA Server Test Suite")
    print("  CDAC ACTS DESD Capstone Project")
    print("=" * 60)
    print(f"  Server: {SERVER_URL}\n")
    
    try:
        test_health_check()
        test_upload_firmware()
        test_get_version()
        test_delta_patch_info()
        test_delta_patch_download()
        test_full_firmware_download()
        
        print("\n" + "=" * 60)
        print("  ✅ ALL TESTS PASSED!")
        print("=" * 60)
    except requests.exceptions.ConnectionError:
        print("\n  ❌ ERROR: Cannot connect to server.")
        print("  Start the server first: python server/ota_server.py")
        sys.exit(1)
    except AssertionError as e:
        print(f"\n  ❌ TEST FAILED: {e}")
        sys.exit(1)
