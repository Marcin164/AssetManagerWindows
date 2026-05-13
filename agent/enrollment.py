"""Agent self-enrollment against POST /devices/agent/enroll."""

from __future__ import annotations

import logging
from typing import Any

import requests

from . import __version__
from .config import AgentConfig
from .fingerprint import collect_fingerprint


log = logging.getLogger(__name__)


def enroll(cfg: AgentConfig) -> dict[str, Any]:
    payload = collect_fingerprint(__version__)
    log.info(
        "Enrolling: tpm=%s cpu=%s serial=%s macs=%d",
        bool(payload.get("tpmFingerprint")),
        bool(payload.get("cpuId")),
        payload.get("serialNumber") or "-",
        len(payload.get("macAddresses") or []),
    )
    verify: Any = cfg.verify_tls
    if cfg.verify_tls and cfg.ca_bundle:
        verify = cfg.ca_bundle

    resp = requests.post(
        f"{cfg.backend_url}/devices/agent/enroll",
        json=payload,
        headers={
            "Content-Type": "application/json",
            "X-Enrollment-Token": cfg.enrollment_token,
            "User-Agent": f"LanVentoryAgent/{__version__} (+windows)",
        },
        timeout=cfg.timeout_seconds,
        verify=verify,
    )
    if resp.status_code >= 400:
        raise RuntimeError(f"Enrollment failed: HTTP {resp.status_code} -- {resp.text[:500]}")
    data = resp.json()
    for k in ("deviceId", "secret"):
        if not data.get(k):
            raise RuntimeError(f"Enrollment response missing field: {k}")
    log.info(
        "Enrolled as %s (matched=%s, reasons=%s)",
        data["deviceId"], data.get("matched"), data.get("matchReasons"),
    )
    return data
