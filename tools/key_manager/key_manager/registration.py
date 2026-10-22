"""FIDO2 credential creation and ESP32 registration."""

import json
import os
from getpass import getpass
from datetime import datetime
from typing import Optional

from fido2.client import ClientError, Fido2Client, UserInteraction
from fido2.hid import CtapHidDevice
from fido2.server import Fido2Server
from fido2.webauthn import (
    PublicKeyCredentialRpEntity,
    PublicKeyCredentialUserEntity,
    ResidentKeyRequirement,
    UserVerificationRequirement,
)

from .serial_comm import SerialConn

RP_ID = "fidolock.local"
RP_NAME = "FidoLock Access Control"

# credProtect level 1 (userVerificationOptional): the door authenticates with
# presence only and sends no allowList. Level 2 would hide the credential from
# such a request, and level 3 would demand a PIN at the door.
CRED_PROTECT_UV_OPTIONAL = 1


class _CliInteraction(UserInteraction):
    def prompt_up(self):
        print("\n>>> Touch your YubiKey to confirm registration <<<\n")

    def request_pin(self, permissions, rp_id):
        return getpass("Enter YubiKey PIN: ")

    def request_uv(self, permissions, rp_id):
        print("User verification required...")
        return True


def find_yubikey() -> Optional[CtapHidDevice]:
    print("Searching for YubiKey...")
    devices = list(CtapHidDevice.list_devices())
    if not devices:
        print("No YubiKey found. Please insert your YubiKey.")
        return None
    device = devices[0]
    print(f"Found YubiKey: {device}")
    return device


def generate_user_id() -> bytes:
    # Random rather than derived from the name: with credProtect 1 the key
    # hands user.id to any NFC reader without a PIN, and a derived handle
    # lets that reader recover the name by hashing guesses (threat-model §5.6).
    # Re-registering the same user on a key therefore adds a credential
    # instead of replacing the old one.
    return os.urandom(16)


def create_credential(device: CtapHidDevice, user_name: str) -> Optional[dict]:
    server = Fido2Server(PublicKeyCredentialRpEntity(id=RP_ID, name=RP_NAME))
    client = Fido2Client(
        device,
        f"https://{RP_ID}",
        user_interaction=_CliInteraction(),
    )

    print(f"YubiKey FIDO2 versions: {client.info.versions}")

    user_id = generate_user_id()
    user = PublicKeyCredentialUserEntity(
        id=user_id, name=user_name, display_name=user_name
    )
    # The credential is discoverable (resident): the door sends no allowList,
    # so the YubiKey must find it by rpId. The key only stores discoverable
    # credentials behind its PIN, so registration asks for it; authentication
    # at the door still does not.
    create_options, state = server.register_begin(
        user=user,
        credentials=[],
        resident_key_requirement=ResidentKeyRequirement.REQUIRED,
        user_verification=UserVerificationRequirement.REQUIRED,
        authenticator_attachment="cross-platform",
        extensions={
            "credentialProtectionPolicy": "userVerificationOptional",
            "enforceCredentialProtectionPolicy": True,
        },
    )

    print("\nCreating resident credential on YubiKey...")
    try:
        result = client.make_credential(create_options["publicKey"])
    except ClientError as e:
        if e.code == ClientError.ERR.CONFIGURATION_UNSUPPORTED:
            print(
                "Resident credentials need a FIDO2 PIN on the key. Set one with "
                "'ykman fido access change-pin'."
            )
            return None
        raise
    auth_data = server.register_complete(
        state, result.client_data, result.attestation_object
    )

    level = (auth_data.extensions or {}).get("credProtect")
    if level is not None and level != CRED_PROTECT_UV_OPTIONAL:
        print(
            f"YubiKey applied credProtect level {level}; the door needs level "
            f"{CRED_PROTECT_UV_OPTIONAL}. Delete this credential with "
            "'ykman fido credentials delete' and retry."
        )
        return None

    print("Credential created successfully!")

    return {
        "credential_id": auth_data.credential_data.credential_id,
        "public_key": auth_data.credential_data.public_key,
        "user_id": user_id,
        "user_name": user_name,
    }


def _format_public_key(cose_key: dict) -> bytes:
    x = cose_key.get(-2, b"").rjust(32, b"\x00")[-32:]
    y = cose_key.get(-3, b"").rjust(32, b"\x00")[-32:]
    return b"\x04" + x + y


def build_reg_payload(credential: dict) -> dict:
    public_key = _format_public_key(credential["public_key"])
    return {
        "cmd": "REGISTER",
        "credential_id": credential["credential_id"].hex(),
        "credential_id_len": len(credential["credential_id"]),
        "public_key": public_key.hex(),
        "user_name": credential["user_name"][:31],
        "timestamp": int(datetime.now().timestamp()),
    }


def register_on_esp32(conn: SerialConn, credential: dict) -> bool:
    payload = build_reg_payload(credential)
    print(f"\nSending credential to ESP32...")
    print(f"  Credential ID: {credential['credential_id'][:8].hex().upper()}...")
    print(f"  User: {credential['user_name']}")
    success, response = conn.send(f"REG:{json.dumps(payload)}")
    if success:
        print("Credential registered on ESP32!")
    else:
        print(f"Registration failed: {response}")
    return success


def export_credential(credential: dict, filename: str) -> bool:
    public_key = _format_public_key(credential["public_key"])
    data = {
        "credential_id": credential["credential_id"].hex(),
        "public_key": public_key.hex(),
        "user_name": credential["user_name"],
        "user_id": credential["user_id"].hex(),
        "created_at": datetime.now().isoformat(),
        "rp_id": RP_ID,
    }
    with open(filename, "w") as f:
        json.dump(data, f, indent=2)
    print(f"Credential exported to: {filename}")
    return True
