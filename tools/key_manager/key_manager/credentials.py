"""Credential management operations against the ESP32."""

import json
from typing import Dict, List, Optional

from .serial_comm import SerialConn


def list_credentials(conn: SerialConn) -> List[Dict]:
    success, response = conn.send("LIST")
    if not success:
        print(f"Failed to list credentials: {response}")
        return []
    credentials = []
    for line in response.splitlines():
        if line.startswith("CRED:"):
            try:
                credentials.append(json.loads(line[5:]))
            except json.JSONDecodeError:
                pass
    return credentials


def delete_credential(conn: SerialConn, credential_id: str) -> bool:
    cid_clean = credential_id.replace(":", "").replace(" ", "").upper()
    success, response = conn.send(f"DELETE:{cid_clean}")
    if success:
        print(f"Credential {cid_clean} deleted.")
    else:
        print(f"Failed to delete: {response}")
    return success


def import_credential(conn: SerialConn, filepath: str) -> bool:
    try:
        with open(filepath) as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"File not found: {filepath}")
        return False
    except json.JSONDecodeError as e:
        print(f"Invalid JSON: {e}")
        return False

    for field in ("credential_id", "public_key", "user_name"):
        if field not in data:
            print(f"Missing field '{field}' in import file")
            return False

    success, response = conn.send(f"REG:{json.dumps(data)}")
    if success:
        print(f"Imported credential for: {data['user_name']}")
    else:
        print(f"Import failed: {response}")
    return success


def get_device_info(conn: SerialConn) -> Optional[Dict]:
    success, response = conn.send("INFO")
    if not success:
        return None
    for line in response.splitlines():
        if line.startswith("INFO:"):
            try:
                return json.loads(line[5:])
            except json.JSONDecodeError:
                pass
    return None


def clear_all(conn: SerialConn) -> bool:
    success, response = conn.send("CLEAR_ALL")
    if success:
        print(f"All credentials cleared.")
    else:
        print(f"Failed to clear credentials: {response}")
    return success


def print_credentials_table(credentials: List[Dict]):
    if not credentials:
        print("\nNo credentials registered.")
        return
    print("\n" + "=" * 80)
    print(f"{'Credential ID':<36} {'User Name':<20} {'Counter':<10} {'Enabled':<10}")
    print("=" * 80)
    for c in credentials:
        print(
            f"{c.get('credential_id', 'N/A')[:34]:<36}"
            f"{c.get('user_name', 'N/A')[:18]:<20}"
            f"{str(c.get('last_counter', 0)):<10}"
            f"{'Yes' if c.get('enabled', True) else 'No':<10}"
        )
    print("=" * 80)
    print(f"Total: {len(credentials)} credential(s)")
