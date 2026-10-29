# key-manager

CLI tool for registering and managing FIDO2 credentials on the FidoLock ESP32.

## Setup

Requires [pyenv](https://github.com/pyenv/pyenv). The directory is pinned to Python 3.11 via `.python-version`.

```bash
# On macOS, point pyenv to Homebrew's OpenSSL to avoid SSL build errors
LDFLAGS="-L$(brew --prefix openssl@3)/lib" \
CPPFLAGS="-I$(brew --prefix openssl@3)/include" \
CONFIGURE_OPTS="--with-openssl=$(brew --prefix openssl@3)" \
# Install Python 3.11 (once)
pyenv install 3.11

# Create virtualenv and install
cd tools/key_manager
make install

# Activate
source .venv/bin/activate
```

After modifying source files, run `make reinstall` to pick up the changes.

## Usage

```
key-manager COMMAND [OPTIONS]
```

Set environment variables to avoid repeating options on every command:

| Variable | Option | Notes |
|----------|--------|-------|
| `FIDOLOCK_PORT` | `--port` | Autodetected if omitted (CP210x / CH340 / FTDI) |
| `FIDOLOCK_UID` | `--uid` | Required for `delete` |

### Commands

| Command | Description |
|---------|-------------|
| `ports` | List available serial ports |
| `register` | Register a security key with the ESP32 |
| `list` | List all registered credentials |
| `delete` | Delete a credential by UID |
| `import` | Import a credential from a JSON file |
| `info` | Show ESP32 device info |
| `clear-all` | Delete all credentials (dangerous) |

Every command accepts `--help` for full option details.

### Examples

```bash
# Find the ESP32 serial port
key-manager ports

# Set env vars once to avoid repeating them
export FIDOLOCK_PORT=/dev/cu.usbserial-0001
export FIDOLOCK_UID=04A1B2C3D4E5F6

# Register a Security Key (touch prompted; backup saved to tmp/<user>.json).
# Creates a resident (discoverable) credential: the YubiKey asks for its FIDO2
# PIN once, here. The door never asks for it.
key-manager register -u "John Doe"

# Register and export only (no ESP32 needed)
key-manager register -u "Jane Doe" --export-only

# Register with a known NFC UID
key-manager register -u "John" --nfc-uid 04A1B2C3D4E5F6

# List credentials
key-manager list

# Delete a credential (uses $FIDOLOCK_UID)
key-manager delete

# Import from backup
key-manager import tmp/john-doe.json

# Device info
key-manager info

# Wipe all credentials
key-manager clear-all
```

## Serial Protocol

| Command | Description | Response |
|---------|-------------|----------|
| `REG:{json}` | Register credential | `OK:Credential registered` |
| `LIST` | List credentials | `CRED:{json}` per entry, `END:count=N` |
| `DELETE:<uid>` | Delete by UID | `OK:Credential deleted` |
| `INFO` | Device info | `INFO:{json}` |
| `CLEAR_ALL` | Wipe all | `OK:All credentials cleared` |

Default baudrate: **115200**.

## Credential JSON Format

```json
{
  "uid": "04A1B2C3D4E5F6",
  "credential_id": "...",
  "public_key": "04...",
  "user_name": "John Doe",
  "user_id": "...",
  "created_at": "2024-01-15T10:30:00",
  "rp_id": "fidolock.local"
}
```

## Notes

- Keep credential backup files secure and never share them.
- Resident credentials need a FIDO2 PIN on the key (`ykman fido access change-pin`) and use one of its discoverable-credential slots (25, or 100 on firmware 5.7+). List or remove them with `ykman fido credentials list` / `delete`.
- Each registration uses a random user handle (`user_id`), because any NFC reader can read it from the key without a PIN. Registering the same user again on the same key therefore adds a credential instead of replacing the old one. Delete the old one from the key with `ykman fido credentials delete`, and from the door with `delete`.
- The NFC UID is read by the ESP32 during authentication. If registering via USB, you may need to update the UID after the first NFC tap using `delete` + `import`.
- On Linux, add udev rules for security key HID access and add your user to the `plugdev` and `dialout` groups.
