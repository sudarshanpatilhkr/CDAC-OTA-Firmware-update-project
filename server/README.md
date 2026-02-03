# OTA Server

Flask-based firmware update server providing REST API for firmware management and delta patch generation.

## Quick Start
```bash
pip install -r requirements.txt
python ota_server.py
```

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Server health check |
| GET | `/api/version` | Get latest firmware version info |
| POST | `/api/firmware/upload` | Upload new firmware binary |
| GET | `/api/firmware/full` | Download full firmware |
| POST | `/api/firmware/delta` | Generate and download delta patch |
| POST | `/api/firmware/delta/info` | Get delta patch statistics |

## Upload Firmware
```bash
curl -X POST -F "file=@firmware.bin" -F "version=1.0.0" http://localhost:5000/api/firmware/upload
```

## Get Delta Patch
```bash
curl -X POST -H "Content-Type: application/json" \
     -d '{"current_version": "1.0.0"}' \
     http://localhost:5000/api/firmware/delta -o patch.bin
```
