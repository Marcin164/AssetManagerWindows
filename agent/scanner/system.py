"""System / OS section -- matches OS.tsx."""

from __future__ import annotations

import getpass
import platform
import socket
from datetime import datetime, timezone
from typing import Any

import psutil

from .helpers import run_powershell, safe


_OS_QUERY = r"""
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version,
    SerialNumber, OSArchitecture, InstallDate, LastBootUpTime, BuildNumber,
    OSLanguage, CountryCode
"""


def collect_system() -> dict[str, Any]:
    cim = safe(run_powershell, None, _OS_QUERY) or {}
    try:
        boot_iso = datetime.fromtimestamp(psutil.boot_time(), tz=timezone.utc).isoformat()
    except Exception:  # noqa: BLE001
        boot_iso = ""
    try:
        user = getpass.getuser()
    except Exception:  # noqa: BLE001
        user = ""
    return {
        "hostname": socket.gethostname(),
        "os_name": platform.system(),
        "os_version": platform.version(),
        "username": user,
        "boot_time": boot_iso,
        "machine": platform.machine(),
        "Cim": cim,
    }
