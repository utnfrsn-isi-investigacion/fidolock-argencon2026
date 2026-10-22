"""Shared serial communication with the FidoLock ESP32."""

import time
from typing import Optional, Tuple

import serial
from serial.tools import list_ports


BAUDRATE = 115200
TIMEOUT = 8  # seconds to wait for a command response

# Emitted by firmware when boot completes and serial commands are ready
_BOOT_READY_MARKER = "Serial commands available"
# How long to wait for a boot sequence before assuming the board is already running
_BOOT_DETECT_S = 1.5   # poll window after releasing DTR/RTS (boot can start up to ~1s later)
_BOOT_MAX_S = 12

_debug = False
_force_reset = False


def set_debug(enabled: bool):
    global _debug
    _debug = enabled


def set_force_reset(enabled: bool):
    global _force_reset
    _force_reset = enabled


def _dbg(msg: str):
    if _debug:
        elapsed = time.monotonic()
        print(f"  [dbg {elapsed:.3f}s] {msg}")


class SerialConn:
    def __init__(self, port: str, baudrate: int = BAUDRATE):
        self.port = port
        self.baudrate = baudrate
        self._serial: Optional[serial.Serial] = None

    def connect(self, force_reset: bool = False) -> bool:
        try:
            _dbg(f"Opening {self.port} at {self.baudrate} baud")
            self._serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=1,  # short per-read timeout; we drive the loop ourselves
            )

            # pyserial asserts DTR+RTS on open by default.
            # With the ESP32 auto-reset circuit: RTS→EN, DTR→IO0.
            # Asserting both holds EN=LOW (chip in reset) for the entire session.
            # Release them immediately so the chip can run.
            _dbg("Releasing DTR/RTS so ESP32 can run")
            self._serial.dtr = False
            self._serial.rts = False

            if force_reset:
                self._dtr_reset()

            # Poll for up to _BOOT_DETECT_S for boot messages.
            # Releasing DTR/RTS may have just started a boot — messages can
            # arrive up to ~1s after the signals change, so a single snapshot
            # is not reliable. We poll in short increments and jump into the
            # boot-wait loop as soon as any bytes appear.
            _dbg(f"Polling for boot messages for up to {_BOOT_DETECT_S}s...")
            detect_deadline = time.time() + _BOOT_DETECT_S
            boot_detected = False
            while time.time() < detect_deadline:
                if self._serial.in_waiting:
                    boot_detected = True
                    break
                time.sleep(0.05)

            _dbg(f"Boot {'detected' if boot_detected else 'not detected'}")
            if boot_detected:
                self._wait_for_boot()
            else:
                _dbg("Board already running")

            # Discard any remaining boot/debug output
            discarded = self._serial.in_waiting
            _dbg(f"Flushing {discarded} remaining bytes from buffer")
            self._serial.reset_input_buffer()
            print(f"Connected to {self.port}")
            return True

        except serial.SerialException as e:
            print(f"Error connecting to {self.port}: {e}")
            return False

    def _dtr_reset(self):
        """Reset ESP32 into app mode via the auto-reset circuit.

        Auto-reset circuit mapping:
          RTS → EN  (RTS=True  → EN=LOW  → reset asserted)
          DTR → IO0 (DTR=True  → IO0=LOW → bootloader mode)

        Keep DTR=False (IO0 HIGH) throughout so the chip boots the app,
        not the bootloader. Only pulse RTS (EN).
        """
        _dbg("Pulsing RTS (EN) to reset ESP32 into app mode...")
        self._serial.dtr = False   # IO0 HIGH = run app
        self._serial.rts = False
        time.sleep(0.05)
        self._serial.rts = True    # EN LOW = hold reset
        time.sleep(0.1)
        self._serial.rts = False   # EN HIGH = release, boot app
        _dbg("Reset pulse done")

    def _wait_for_boot(self):
        """Read boot output until the ready marker appears or timeout."""
        print("ESP32 is booting, waiting for ready...")
        deadline = time.time() + _BOOT_MAX_S
        while time.time() < deadline:
            if self._serial.in_waiting:
                line = self._serial.readline().decode("utf-8", errors="ignore").strip()
                _dbg(f"boot< {line!r}")
                if _BOOT_READY_MARKER in line:
                    _dbg("Boot marker found")
                    return
            else:
                time.sleep(0.05)
        print("Warning: boot timeout, proceeding anyway")

    def disconnect(self):
        if self._serial and self._serial.is_open:
            self._serial.close()
            _dbg("Port closed")

    def __enter__(self):
        if not self.connect(force_reset=_force_reset):
            raise RuntimeError(f"Could not open {self.port}")
        return self

    def __exit__(self, *_):
        self.disconnect()

    def send(self, command: str) -> Tuple[bool, str]:
        if not self._serial or not self._serial.is_open:
            return False, "Not connected"
        try:
            _dbg(f">>> {command!r}  (in_waiting before write: {self._serial.in_waiting})")
            self._serial.write(f"{command}\n".encode())
            self._serial.flush()
            _dbg(f"write done, in_waiting after flush: {self._serial.in_waiting}")

            # Use readline() directly — avoids unreliable in_waiting polling.
            # readline() blocks for up to serial.timeout (1s) per call waiting
            # for a newline, then returns whatever it has (possibly empty bytes).
            lines: list[str] = []
            deadline = time.time() + TIMEOUT
            while time.time() < deadline:
                raw = self._serial.readline()
                _dbg(f"readline() → {len(raw)} bytes: {raw!r}  in_waiting={self._serial.in_waiting}")
                if raw:
                    line = raw.decode("utf-8", errors="ignore").strip()
                    if line:
                        lines.append(line)
                    if line.startswith(("OK", "ERROR", "END")):
                        break
            else:
                _dbg(f"Timeout after {TIMEOUT}s — {len(lines)} line(s) received")

            success = any(l.startswith(("OK", "END")) for l in lines)
            _dbg(f"success={success}, lines={lines}")
            return success, "\n".join(lines)
        except Exception as e:
            return False, str(e)


# USB VIDs for common ESP32 USB-UART bridge chips
_ESP32_VIDS = {
    0x10C4,  # Silicon Labs CP210x
    0x1A86,  # QinHeng CH340/CH341
    0x0403,  # FTDI
}


def autodetect_port() -> Optional[str]:
    """Return the first serial port that looks like an ESP32 bridge, or None."""
    for p in list_ports.comports():
        if p.vid in _ESP32_VIDS:
            _dbg(f"Autodetected ESP32 port: {p.device} ({p.description})")
            return p.device
    return None


def list_serial_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    print("\nAvailable serial ports:")
    for p in ports:
        print(f"  {p.device}: {p.description}")
