"""LanVentory manager: read-only status window with on-demand actions."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QFileSystemWatcher, QTimer, Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QApplication,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from gui import agent_io


def _format_dt(value: Optional[str]) -> str:
    if not value:
        return "never"
    return value.replace("T", " ").split(".")[0]


class ManagerWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("LanVentory Agent")
        self.resize(820, 600)

        central = QWidget()
        outer = QVBoxLayout(central)

        status_group = QGroupBox("Status")
        grid = QGridLayout(status_group)
        self._labels: dict[str, QLabel] = {}
        rows = [
            ("Backend URL:",    "backend"),
            ("Device ID:",      "device_id"),
            ("Enrolled at:",    "enrolled_at"),
            ("Matched:",        "matched"),
            ("Match reasons:",  "match_reasons"),
            ("Last log line:",  "last_log"),
            ("Config file:",    "config_path"),
            ("State file:",     "state_path"),
            ("Log file:",       "log_path"),
        ]
        for row, (caption, key) in enumerate(rows):
            grid.addWidget(QLabel(caption), row, 0, alignment=Qt.AlignmentFlag.AlignTop)
            val = QLabel("-")
            val.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            val.setWordWrap(True)
            grid.addWidget(val, row, 1)
            self._labels[key] = val
        grid.setColumnStretch(1, 1)
        outer.addWidget(status_group)

        button_row = QHBoxLayout()
        self.btn_scan = QPushButton("Scan now")
        self.btn_scan.clicked.connect(self._scan_now)
        self.btn_reenroll = QPushButton("Re-enroll")
        self.btn_reenroll.clicked.connect(self._reenroll)
        self.btn_open_log = QPushButton("Open log")
        self.btn_open_log.clicked.connect(self._open_log)
        self.btn_open_dir = QPushButton("Open config folder")
        self.btn_open_dir.clicked.connect(self._open_config_dir)
        self.btn_refresh = QPushButton("Refresh")
        self.btn_refresh.clicked.connect(self._refresh)
        for b in (
            self.btn_scan,
            self.btn_reenroll,
            self.btn_open_log,
            self.btn_open_dir,
            self.btn_refresh,
        ):
            button_row.addWidget(b)
        button_row.addStretch(1)
        outer.addLayout(button_row)

        log_group = QGroupBox("Agent log (tail)")
        log_layout = QVBoxLayout(log_group)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setFont(QFont("Consolas", 9))
        log_layout.addWidget(self.log_view)
        outer.addWidget(log_group, stretch=1)

        self.setCentralWidget(central)
        self.setStatusBar(QStatusBar())

        self._watcher = QFileSystemWatcher(self)
        for p in (str(agent_io.CONFIG_PATH), str(agent_io.STATE_PATH)):
            if Path(p).exists():
                self._watcher.addPath(p)
        self._watcher.fileChanged.connect(self._refresh)

        self._tick = QTimer(self)
        self._tick.setInterval(5000)
        self._tick.timeout.connect(self._refresh)
        self._tick.start()

        self._refresh()

    def _refresh(self) -> None:
        cfg = agent_io.load_config_raw()
        backend = cfg.get("backend_url") if cfg else None
        self._labels["backend"].setText(backend or "<not configured>")

        state = agent_io.load_state_raw()
        if state is None:
            self._labels["device_id"].setText("(not enrolled yet)")
            self._labels["enrolled_at"].setText("-")
            self._labels["matched"].setText("-")
            self._labels["match_reasons"].setText("-")
        else:
            self._labels["device_id"].setText(state.get("device_id", "-"))
            self._labels["enrolled_at"].setText(_format_dt(state.get("enrolled_at")))
            self._labels["matched"].setText(str(state.get("matched", "?")))
            self._labels["match_reasons"].setText(
                ", ".join(state.get("match_reasons") or []) or "-"
            )

        self._labels["config_path"].setText(str(agent_io.CONFIG_PATH))
        self._labels["state_path"].setText(str(agent_io.STATE_PATH))

        log_path = agent_io.resolve_log_path()
        self._labels["log_path"].setText(str(log_path) if log_path else "-")

        last_line, tail = self._read_log_tail(log_path)
        self._labels["last_log"].setText(last_line or "-")
        if tail is not None:
            self.log_view.setPlainText(tail)
            self.log_view.verticalScrollBar().setValue(
                self.log_view.verticalScrollBar().maximum()
            )

    def _read_log_tail(
        self, path: Optional[Path], max_bytes: int = 32_000,
    ) -> tuple[Optional[str], Optional[str]]:
        if not path or not path.exists():
            return None, None
        try:
            size = path.stat().st_size
            with path.open("rb") as f:
                if size > max_bytes:
                    f.seek(-max_bytes, os.SEEK_END)
                data = f.read().decode("utf-8", errors="replace")
        except Exception as err:  # noqa: BLE001
            return f"<log read error: {err}>", None
        lines = [line for line in data.splitlines() if line.strip()]
        last = lines[-1] if lines else None
        return last, "\n".join(lines[-400:])

    def _scan_now(self) -> None:
        try:
            agent_io.run_task_now()
            self.statusBar().showMessage("Scan triggered via Task Scheduler.", 4000)
        except Exception as err:  # noqa: BLE001
            QMessageBox.warning(
                self, "Scan failed",
                f"Could not trigger the scheduled task:\n\n{err}\n\n"
                "Tip: schtasks /Run requires the task to be registered "
                "(run the installer first).",
            )

    def _reenroll(self) -> None:
        confirm = QMessageBox.question(
            self, "Re-enroll?",
            "This will discard the current state.json and request a new "
            "device id + secret from the backend.\n\n"
            "If the host's fingerprint still matches an existing device "
            "in LanVentory, it will be re-bound to the same record.\n\n"
            "Continue?",
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return
        if not agent_io.AGENT_EXE.exists():
            QMessageBox.warning(
                self, "Agent missing",
                f"Cannot find {agent_io.AGENT_EXE}.",
            )
            return
        try:
            subprocess.Popen(
                [str(agent_io.AGENT_EXE),
                 "--enroll-only", "--force-enroll", "--verbose"],
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            self.statusBar().showMessage(
                "Re-enrollment started -- watch the log.", 6000)
        except Exception as err:  # noqa: BLE001
            QMessageBox.warning(self, "Re-enroll failed", str(err))

    def _open_log(self) -> None:
        path = agent_io.resolve_log_path()
        if not path or not path.exists():
            QMessageBox.information(self, "No log", "No log file yet.")
            return
        os.startfile(str(path))  # type: ignore[attr-defined]

    def _open_config_dir(self) -> None:
        agent_io.PROGRAM_DATA_DIR.mkdir(parents=True, exist_ok=True)
        os.startfile(str(agent_io.PROGRAM_DATA_DIR))  # type: ignore[attr-defined]


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("LanVentory Agent")
    win = ManagerWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
