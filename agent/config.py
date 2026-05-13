"""Config + state files on disk.

``config.json`` -- backend URL + bootstrap enrollment token. Either
written by the Inno Setup installer's CLI args step, or by the
operator's PowerShell snippet. Token can be plaintext or DPAPI blob.

``state.json`` -- written by the agent itself after a successful
enrollment: device id + DPAPI-encrypted agent secret.
"""

from __future__ import annotations

import base64
import datetime as _dt
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


PROGRAM_DATA_DIR = Path(
    os.environ.get("PROGRAMDATA", r"C:\ProgramData")
) / "LanVentory" / "agent"

DEFAULT_CONFIG_PATH = PROGRAM_DATA_DIR / "config.json"
DEFAULT_STATE_PATH = PROGRAM_DATA_DIR / "state.json"


@dataclass
class AgentConfig:
    backend_url: str
    enrollment_token: str
    interval_minutes: int = 60
    verify_tls: bool = True
    ca_bundle: Optional[str] = None
    timeout_seconds: int = 60
    log_path: Optional[str] = None


@dataclass
class AgentState:
    device_id: str
    secret: str


def _dpapi_decrypt(blob_b64: str) -> str:
    if not blob_b64.startswith("dpapi:"):
        return blob_b64
    from . import dpapi as _dpapi

    payload = base64.b64decode(blob_b64.removeprefix("dpapi:"))
    return _dpapi.unprotect(payload).decode("utf-8")


def dpapi_encrypt(plaintext: str) -> str:
    if sys.platform != "win32":
        return plaintext
    from . import dpapi as _dpapi

    blob = _dpapi.protect(plaintext.encode("utf-8"))
    return "dpapi:" + base64.b64encode(blob).decode("ascii")


def load_config(path: Path = DEFAULT_CONFIG_PATH) -> AgentConfig:
    if not path.exists():
        raise FileNotFoundError(
            f"Config not found at {path}. Pass /BACKENDURL=... /TOKEN=... "
            "to the installer or rerun setup."
        )
    raw = json.loads(path.read_text(encoding="utf-8"))
    for key in ("backend_url", "enrollment_token"):
        if not raw.get(key):
            raise ValueError(f"Config missing required key: {key}")
    return AgentConfig(
        backend_url=raw["backend_url"].rstrip("/"),
        enrollment_token=_dpapi_decrypt(raw["enrollment_token"]),
        interval_minutes=int(raw.get("interval_minutes", 60)),
        verify_tls=bool(raw.get("verify_tls", True)),
        ca_bundle=raw.get("ca_bundle") or None,
        timeout_seconds=int(raw.get("timeout_seconds", 60)),
        log_path=raw.get("log_path") or None,
    )


def load_state(path: Path = DEFAULT_STATE_PATH) -> Optional[AgentState]:
    if not path.exists():
        return None
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None
    device_id = raw.get("device_id")
    secret_blob = raw.get("secret")
    if not device_id or not secret_blob:
        return None
    try:
        return AgentState(device_id=device_id, secret=_dpapi_decrypt(secret_blob))
    except Exception:
        return None


def write_state(
    path: Path,
    *,
    device_id: str,
    secret_plaintext: str,
    matched: bool,
    match_reasons: Optional[list[str]] = None,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "device_id": device_id,
        "secret": dpapi_encrypt(secret_plaintext),
        "matched": matched,
        "match_reasons": match_reasons or [],
        "enrolled_at": _dt.datetime.now(tz=_dt.timezone.utc).isoformat(),
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
