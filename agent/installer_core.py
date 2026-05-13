"""Scheduled task management via schtasks.exe.

Called by ``lanventory-agent.exe --register-task`` / ``--unregister-task``
from the Inno Setup [Run] / [UninstallRun] sections.
"""

from __future__ import annotations

import logging
import os
import subprocess
import tempfile
from pathlib import Path


log = logging.getLogger(__name__)

TASK_NAME = "LanVentoryAgent"


_TASK_XML = """<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>LanVentory inventory agent.</Description>
    <Author>LanVentory</Author>
  </RegistrationInfo>
  <Triggers>
    <BootTrigger><Enabled>true</Enabled><Delay>PT2M</Delay></BootTrigger>
    <TimeTrigger>
      <Repetition>
        <Interval>PT{interval}M</Interval>
        <StopAtDurationEnd>false</StopAtDurationEnd>
      </Repetition>
      <StartBoundary>2024-01-01T00:00:00</StartBoundary>
      <Enabled>true</Enabled>
    </TimeTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>S-1-5-18</UserId>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <StartWhenAvailable>true</StartWhenAvailable>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <ExecutionTimeLimit>PT30M</ExecutionTimeLimit>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>{exe}</Command>
      <Arguments>--once</Arguments>
    </Exec>
  </Actions>
</Task>
"""


def _run_schtasks(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["schtasks.exe", *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )


def register_scheduled_task(*, exe_path: Path, interval_minutes: int = 60,
                            task_name: str = TASK_NAME) -> None:
    xml = _TASK_XML.format(exe=str(exe_path), interval=int(interval_minutes))
    fd, tmp_path = tempfile.mkstemp(prefix="lanventory-task-", suffix=".xml")
    try:
        with os.fdopen(fd, "w", encoding="utf-16") as f:
            f.write(xml)
        proc = _run_schtasks(["/Create", "/TN", task_name, "/XML", tmp_path, "/F"])
        if proc.returncode != 0:
            raise RuntimeError(
                f"schtasks /Create failed (rc={proc.returncode}): "
                f"{(proc.stderr or proc.stdout).strip()[:500]}"
            )
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def unregister_scheduled_task(task_name: str = TASK_NAME) -> bool:
    proc = _run_schtasks(["/Delete", "/TN", task_name, "/F"])
    if proc.returncode == 0:
        return True
    out = (proc.stderr or proc.stdout or "").lower()
    if "cannot find" in out or "does not exist" in out:
        return False
    raise RuntimeError(
        f"schtasks /Delete failed (rc={proc.returncode}): "
        f"{(proc.stderr or proc.stdout).strip()[:500]}"
    )
