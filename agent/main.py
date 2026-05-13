"""LanVentory Windows agent entry point.

Flow per invocation:

1. Load ``config.json`` (backend URL + bootstrap enrollment token).
2. Load ``state.json``. If missing/corrupt, self-enroll via
   ``POST /devices/agent/enroll`` and persist (device_id, secret).
3. Run requested section scanners and push payload to
   ``POST /devices/agent/data`` with an HMAC signature.

Installer (Inno Setup) passes ``--register-task`` / ``--unregister-task``
for scheduled-task lifecycle.
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
import time
import traceback
from pathlib import Path
from typing import Any, Callable

from . import scanner
from .config import (
    DEFAULT_CONFIG_PATH, DEFAULT_STATE_PATH,
    AgentConfig, AgentState, dpapi_encrypt, load_config, load_state, write_state,
)
from .enrollment import enroll
from .transport import send_scan


SECTION_COLLECTORS: dict[str, Callable[[], Any]] = {
    "system":           scanner.collect_system,
    "hardware":         scanner.collect_hardware,
    "software":         scanner.collect_software,
    "network":          scanner.collect_network,
    "security":         scanner.collect_security,
    "peripherals":      scanner.collect_peripherals,
    "events":           scanner.collect_events,
    "users_and_groups": scanner.collect_users,
}


def _setup_logging(verbose: bool, log_path: str | None) -> None:
    handlers: list[logging.Handler] = [logging.StreamHandler(sys.stderr)]
    if log_path:
        Path(log_path).parent.mkdir(parents=True, exist_ok=True)
        handlers.append(logging.FileHandler(log_path, encoding="utf-8"))
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
        handlers=handlers,
    )


def build_payload(sections: list[str]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    log = logging.getLogger("agent.scan")
    for name in sections:
        collector = SECTION_COLLECTORS.get(name)
        if collector is None:
            log.warning("Unknown section: %s -- skipping", name)
            continue
        try:
            log.info("Collecting %s", name)
            out[name] = collector()
        except Exception as err:  # noqa: BLE001
            log.error("Section %s failed: %s\n%s", name, err, traceback.format_exc())
    return out


def ensure_enrolled(cfg: AgentConfig, state_path: Path, force: bool = False) -> AgentState:
    log = logging.getLogger("agent.enroll")
    state = None if force else load_state(state_path)
    if state is not None:
        log.debug("Using existing enrollment: %s", state.device_id)
        return state
    log.info("No usable state -- performing enrollment.")
    result = enroll(cfg)
    write_state(
        state_path,
        device_id=result["deviceId"], secret_plaintext=result["secret"],
        matched=bool(result.get("matched")),
        match_reasons=result.get("matchReasons") or [],
    )
    log.info("Persisted state (device=%s, matched=%s)", result["deviceId"], result.get("matched"))
    return AgentState(device_id=result["deviceId"], secret=result["secret"])


def run_once(cfg: AgentConfig, state: AgentState, sections: list[str], dry_run: bool) -> int:
    payload = build_payload(sections)
    if dry_run:
        json.dump(payload, sys.stdout, indent=2, default=str)
        sys.stdout.write("\n")
        return 0
    result = send_scan(cfg, state, payload)
    logging.getLogger("agent").info("Scan accepted: %s", result)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="lanventory-agent")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_PATH)
    parser.add_argument("--state",  type=Path, default=DEFAULT_STATE_PATH)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--enroll-only", action="store_true")
    parser.add_argument("--force-enroll", action="store_true")
    parser.add_argument("--encrypt-stdin", action="store_true",
                        help="Read stdin, print DPAPI-encrypted form.")
    parser.add_argument("--register-task", action="store_true",
                        help="Register the LanVentoryAgent scheduled task.")
    parser.add_argument("--unregister-task", action="store_true",
                        help="Remove the LanVentoryAgent scheduled task.")
    parser.add_argument("--interval", type=int, default=60)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--sections", type=str,
                        default=",".join(SECTION_COLLECTORS.keys()))
    args = parser.parse_args(argv)

    if args.encrypt_stdin:
        sys.stdout.write(dpapi_encrypt(sys.stdin.read().strip()))
        return 0

    if args.register_task:
        from .installer_core import register_scheduled_task
        register_scheduled_task(exe_path=Path(sys.executable), interval_minutes=args.interval)
        return 0

    if args.unregister_task:
        from .installer_core import unregister_scheduled_task
        unregister_scheduled_task()
        return 0

    if not (args.once or args.watch or args.dry_run or args.enroll_only):
        parser.error("Specify one of --once / --watch / --dry-run / --enroll-only.")

    sections = [s.strip() for s in args.sections.split(",") if s.strip()]

    if args.dry_run:
        _setup_logging(args.verbose, None)
        return run_once(
            AgentConfig(backend_url="", enrollment_token=""),
            AgentState(device_id="dry", secret="dry"),
            sections, dry_run=True,
        )

    cfg = load_config(args.config)
    _setup_logging(args.verbose, cfg.log_path)
    state = ensure_enrolled(cfg, args.state, force=args.force_enroll)
    if args.enroll_only:
        return 0
    if args.once:
        return run_once(cfg, state, sections, dry_run=False)

    # --watch
    log = logging.getLogger("agent")
    while True:
        try:
            run_once(cfg, state, sections, dry_run=False)
        except Exception as err:  # noqa: BLE001
            log.exception("Scan failed: %s", err)
        sleep_for = max(60, cfg.interval_minutes * 60)
        log.info("Sleeping %ds before next scan.", sleep_for)
        time.sleep(sleep_for)


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
