"""Shared helpers -- PowerShell invocation + safe defaults."""

from __future__ import annotations

import json
import logging
import subprocess
from typing import Any


log = logging.getLogger(__name__)


def run_powershell(script: str, timeout: int = 90) -> Any:
    """Run a PowerShell script that emits JSON on stdout. Returns the
    parsed value, or raises on PS failure. Each scan section calls this
    multiple times -- wrapping minimises subprocess churn."""
    wrapped = (
        "$ErrorActionPreference='Stop';"
        "try {"
        f"  $r = & {{ {script} }};"
        "  if ($null -eq $r) { '' } else {"
        "    $r | ConvertTo-Json -Depth 6 -Compress"
        "  }"
        "} catch { Write-Error $_; exit 1 }"
    )
    proc = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile", "-NonInteractive",
            "-ExecutionPolicy", "Bypass",
            "-OutputFormat", "Text",
            "-Command", wrapped,
        ],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=timeout,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"PowerShell failed (rc={proc.returncode}): "
            f"{(proc.stderr or '').strip()[:500]}"
        )
    out = (proc.stdout or "").strip()
    if not out:
        return None
    try:
        return json.loads(out)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"PowerShell did not return JSON: {err}; first 200: {out[:200]}")


def as_list(value: Any) -> list[Any]:
    """ConvertTo-Json collapses single-element arrays. Use this whenever
    the consumer expects a list."""
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def safe(fn, default, *args, **kwargs):
    """Call ``fn`` and fall back to ``default`` on any error."""
    try:
        return fn(*args, **kwargs)
    except Exception as err:  # noqa: BLE001
        log.warning("collector %s failed: %s", getattr(fn, "__name__", fn), err)
        return default
