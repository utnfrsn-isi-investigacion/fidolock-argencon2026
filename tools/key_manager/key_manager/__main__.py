"""FidoLock Key Manager CLI."""

import os
import sys
import click

from .serial_comm import SerialConn, list_serial_ports, autodetect_port, set_debug, set_force_reset
from . import credentials as cred_mgr
from . import registration as reg


# ── shared options ─────────────────────────────────────────────────────────────

def port_options(required=True):
    def decorator(f):
        f = click.option("--baudrate", "-b", default=115200, show_default=True,
                         help="Serial baudrate")(f)
        f = click.option("--port", "-p", required=False, envvar="FIDOLOCK_PORT",
                         help="Serial port. Env: FIDOLOCK_PORT (autodetected if omitted)")(f)
        return f
    return decorator


def _resolve_port(port: str | None) -> str:
    if port:
        return port
    detected = autodetect_port()
    if detected:
        click.echo(f"Autodetected port: {detected}")
        return detected
    raise click.UsageError(
        "No ESP32 port found. Plug in the device or set FIDOLOCK_PORT."
    )


# ── CLI root ───────────────────────────────────────────────────────────────────

@click.group()
@click.option("--debug", "-d", is_flag=True, envvar="FIDOLOCK_DEBUG",
              help="Show raw serial traffic. Env: FIDOLOCK_DEBUG")
@click.option("--reset", "-r", is_flag=True,
              help="Pulse DTR to reboot the ESP32 before sending commands")
def cli(debug=False, reset=False):
    """FidoLock key manager – register and manage FIDO2 credentials on ESP32."""
    if debug:
        set_debug(True)
    if reset:
        set_force_reset(True)


# ── ports ──────────────────────────────────────────────────────────────────────

@cli.command()
def ports():
    """List available serial ports."""
    list_serial_ports()


# ── register ───────────────────────────────────────────────────────────────────

@cli.command()
@port_options(required=False)
@click.option("--user", "-u", required=True, help="User name for the credential")
@click.option("--output", "-o", default=None,
              help="Backup JSON file path (default: tmp/<user>.json)")
@click.option("--export-only", "-e", is_flag=True,
              help="Create credential only, skip sending to ESP32")
def register(port, baudrate, user, output, export_only):
    """Register a YubiKey with the ESP32."""
    if output is None:
        os.makedirs("tmp", exist_ok=True)
        output = f"tmp/{user}.json"

    device = reg.find_yubikey()
    if not device:
        sys.exit(1)

    credential = reg.create_credential(device, user)
    if not credential:
        sys.exit(1)

    reg.export_credential(credential, output)

    if not export_only:
        try:
            with SerialConn(_resolve_port(port), baudrate) as conn:
                if not reg.register_on_esp32(conn, credential):
                    click.echo(f"Credential saved to {output}; use 'import' to retry.", err=True)
                    sys.exit(1)
        except RuntimeError as e:
            raise click.ClickException(str(e))

    click.echo(f"\nUser '{user}' registered. Backup: {output}")


# ── list ───────────────────────────────────────────────────────────────────────

@cli.command("list")
@port_options()
def list_creds(port, baudrate):
    """List all registered credentials."""
    with SerialConn(_resolve_port(port), baudrate) as conn:
        creds = cred_mgr.list_credentials(conn)
    cred_mgr.print_credentials_table(creds)


# ── delete ─────────────────────────────────────────────────────────────────────

@cli.command()
@port_options()
@click.option("--credential-id", required=True, envvar="FIDOLOCK_CREDENTIAL_ID",
              help="Credential ID to delete (hex). Env: FIDOLOCK_CREDENTIAL_ID")
def delete(port, baudrate, credential_id):
    """Delete a credential by credential ID."""
    with SerialConn(_resolve_port(port), baudrate) as conn:
        ok = cred_mgr.delete_credential(conn, credential_id)
    sys.exit(0 if ok else 1)


# ── import ─────────────────────────────────────────────────────────────────────

@cli.command("import")
@port_options()
@click.argument("file", type=click.Path(exists=True))
def import_cred(port, baudrate, file):
    """Import a credential from a JSON file."""
    with SerialConn(_resolve_port(port), baudrate) as conn:
        ok = cred_mgr.import_credential(conn, file)
    sys.exit(0 if ok else 1)


# ── info ───────────────────────────────────────────────────────────────────────

@cli.command()
@port_options()
def info(port, baudrate):
    """Show ESP32 device information."""
    with SerialConn(_resolve_port(port), baudrate) as conn:
        device_info = cred_mgr.get_device_info(conn)
    if not device_info:
        raise click.ClickException("Failed to get device info.")
    click.echo("\nDevice Information:")
    click.echo(f"  Firmware:     {device_info.get('version', 'N/A')}")
    click.echo(f"  Credentials:  {device_info.get('credential_count', 'N/A')}")
    click.echo(f"  Free storage: {device_info.get('free_storage', 'N/A')} bytes")
    click.echo(f"  Uptime:       {device_info.get('uptime', 'N/A')} s")


# ── clear-all ──────────────────────────────────────────────────────────────────

@cli.command("clear-all")
@port_options()
def clear_all(port, baudrate):
    """Delete ALL credentials from the ESP32 (dangerous)."""
    click.confirm("Delete ALL credentials?", abort=True)
    with SerialConn(_resolve_port(port), baudrate) as conn:
        ok = cred_mgr.clear_all(conn)
    click.echo("All credentials cleared." if ok else "Failed.")
    sys.exit(0 if ok else 1)


# ── entry point ────────────────────────────────────────────────────────────────

def main():
    cli()


if __name__ == "__main__":
    main()
