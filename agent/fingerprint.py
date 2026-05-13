"""Stable hardware identifiers used by /devices/agent/enroll.

Same shape as backend's extractFingerprint(): TPM hash, MACs, CPU id,
baseboard serial, hostname.
"""

from __future__ import annotations

import hashlib
import platform
import socket
import uuid
from typing import Any

import psutil

from .scanner.helpers import run_powershell, safe


_TPM_EK_PS = r"""
try {
  $ek = Get-TpmEndorsementKeyInfo -ErrorAction Stop
  if ($ek -and $ek.PublicKey) {
    try {
      [Convert]::ToBase64String($ek.PublicKey.ExportSubjectPublicKeyInfo())
    } catch {
      if ($ek.PublicKeyHash) { $ek.PublicKeyHash } else { '' }
    }
  }
} catch {}
"""

_BASEBOARD_PS = r"""
$b = Get-CimInstance Win32_BaseBoard -ErrorAction SilentlyContinue | Select-Object -First 1
if ($b) {
  [pscustomobject]@{
    serial_number = $b.SerialNumber; manufacturer = $b.Manufacturer; product = $b.Product
  }
}
"""

_CPU_ID_PS = r"""
$c = Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1
if ($c) { $c.ProcessorId }
"""


def _tpm_fingerprint() -> str | None:
    ek = safe(run_powershell, None, _TPM_EK_PS)
    if not isinstance(ek, str) or not ek.strip():
        return None
    return hashlib.sha256(ek.strip().encode("utf-8")).hexdigest()


def _mac_addresses() -> list[str]:
    seen: set[str] = set()
    for addrs in psutil.net_if_addrs().values():
        for a in addrs:
            v = (a.address or "").strip()
            if len(v) != 17:
                continue
            norm = v.upper().replace("-", ":")
            if norm in {"00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF"}:
                continue
            if all(c in "0123456789ABCDEF:" for c in norm):
                seen.add(norm)
    return sorted(seen)


def collect_fingerprint(agent_version: str) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "hostname": socket.gethostname(),
        "platform": "windows",
        "agentVersion": agent_version,
    }
    if tpm := _tpm_fingerprint():
        payload["tpmFingerprint"] = tpm
    if macs := _mac_addresses():
        payload["macAddresses"] = macs
    cpu = safe(run_powershell, None, _CPU_ID_PS)
    if isinstance(cpu, str) and cpu.strip():
        payload["cpuId"] = cpu.strip()
    board = safe(run_powershell, None, _BASEBOARD_PS) or {}
    if board.get("serial_number"):
        payload["serialNumber"] = board["serial_number"]
    if board.get("manufacturer"):
        payload["manufacturer"] = board["manufacturer"]
    if board.get("product"):
        payload["model"] = board["product"]

    if not any(k in payload for k in ("tpmFingerprint", "cpuId", "serialNumber", "macAddresses")):
        payload["cpuId"] = "fallback-" + str(uuid.uuid5(uuid.NAMESPACE_DNS, socket.gethostname()))
    return payload
