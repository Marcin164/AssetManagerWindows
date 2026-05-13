"""Thin client wrapping the native ``lanventory-agent.exe``.

The headless agent now lives in agent-cpp/ as a C++ binary. The two
GUI tools talk to it through the on-disk files (config.json, state.json)
and through the agent's CLI flags. We deliberately don't link any
Python crypto code here -- the agent owns DPAPI encryption via the
``--encrypt-stdin`` subcommand, and the manager only ever reads metadata
that's safe to expose in plaintext (device id, enrollment timestamps).
"""

from __future__ import annotations

import ctypes
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Callable, Optional


PROGRAM_DATA_DIR = Path(
    os.environ.get("PROGRAMDATA", r"C:\ProgramData")
) / "LanVentory" / "agent"
INSTALL_DIR = Path(
    os.environ.get("PROGRAMFILES", r"C:\Program Files")
) / "LanVentory" / "agent"

CONFIG_PATH = PROGRAM_DATA_DIR / "config.json"
STATE_PATH  = PROGRAM_DATA_DIR / "state.json"
AGENT_EXE   = INSTALL_DIR / "lanventory-agent.exe"

TASK_NAME = "LanVentoryAgent"


def is_elevated() -> bool:
    if sys.platform != "win32":
        return False
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:  # noqa: BLE001
        return False


def relaunch_elevated() -> int:
    if sys.platform != "win32":
        raise RuntimeError("UAC relaunch only available on Windows")
    params = " ".join(f'"{a}"' for a in sys.argv[1:])
    return int(
        ctypes.windll.shell32.ShellExecuteW(
            None, "runas", sys.executable, params, None, 1
        )
    )


def _no_window_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0)


def dpapi_encrypt_via_agent(plaintext: str) -> str:
    """Pipe plaintext into ``lanventory-agent.exe --encrypt-stdin``. The
    agent owns DPAPI so the encrypted blob format matches what its
    runtime decrypter expects."""
    proc = subprocess.run(
        [str(AGENT_EXE), "--encrypt-stdin"],
        input=plaintext,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=_no_window_flags(),
    )
    if proc.returncode != 0 or not proc.stdout.startswith("dpapi:"):
        raise RuntimeError(
            f"agent --encrypt-stdin failed (rc={proc.returncode}): "
            f"{(proc.stderr or proc.stdout).strip()[:300]}"
        )
    return proc.stdout.strip()


def write_config(
    *,
    backend_url: str,
    enrollment_token_plaintext: str,
    verify_tls: bool = True,
    ca_bundle: Optional[str] = None,
    interval_minutes: int = 60,
) -> None:
    PROGRAM_DATA_DIR.mkdir(parents=True, exist_ok=True)
    payload = {
        "backend_url": backend_url.rstrip("/"),
        "enrollment_token": dpapi_encrypt_via_agent(enrollment_token_plaintext),
        "interval_minutes": interval_minutes,
        "verify_tls": verify_tls,
        "ca_bundle": ca_bundle,
        "log_path": str(PROGRAM_DATA_DIR / "agent.log"),
    }
    CONFIG_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def lock_down_acl(path: Path) -> None:
    """Restrict ``path`` to SYSTEM + Administrators using icacls.
    Best-effort -- warnings are logged but failures don't fault."""
    for args in (
        ["/inheritance:r"],
        ["/grant", "SYSTEM:(F)"],
        ["/grant", "Administrators:(F)"],
    ):
        subprocess.run(
            ["icacls.exe", str(path), *args],
            capture_output=True, text=True,
            creationflags=_no_window_flags(),
        )


def load_state_raw() -> Optional[dict]:
    if not STATE_PATH.exists():
        return None
    try:
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001
        return None


def load_config_raw() -> Optional[dict]:
    if not CONFIG_PATH.exists():
        return None
    try:
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001
        return None


def run_agent_streaming(
    *args: str,
    on_line: Callable[[str], None] = lambda _: None,
    timeout: int = 120,
) -> bool:
    """Spawn the agent with the given args, stream stdout+stderr line by
    line through ``on_line``. Returns True on rc=0."""
    if not AGENT_EXE.exists():
        on_line(f"Agent binary not found at {AGENT_EXE}.")
        return False
    proc = subprocess.Popen(
        [str(AGENT_EXE), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=_no_window_flags(),
    )
    assert proc.stdout is not None
    try:
        for line in proc.stdout:
            on_line(line.rstrip())
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        on_line(f"Agent timed out after {timeout}s.")
        return False
    return proc.returncode == 0


def run_task_now() -> None:
    proc = subprocess.run(
        ["schtasks.exe", "/Run", "/TN", TASK_NAME],
        capture_output=True, text=True,
        creationflags=_no_window_flags(),
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"schtasks /Run failed (rc={proc.returncode}): "
            f"{(proc.stderr or proc.stdout).strip()[:300]}"
        )


def resolve_log_path() -> Optional[Path]:
    cfg = load_config_raw()
    if cfg and cfg.get("log_path"):
        return Path(cfg["log_path"])
    candidate = PROGRAM_DATA_DIR / "agent.log"
    return candidate if candidate.exists() else None
