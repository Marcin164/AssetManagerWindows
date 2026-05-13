"""Post-install configurator.

Launched after the Inno Setup installer (auto via the Finish-page
checkbox) or from the Start Menu shortcut. Collects backend URL +
enrollment token, persists them through ``lanventory-agent.exe``'s
``--encrypt-stdin`` helper, then runs ``--enroll-only`` to validate
the input end-to-end.

Self-elevates via UAC because writing under %ProgramData% (when the
installer created the dir with SYSTEM-only ACL) and ``icacls`` lockdown
both need Administrator rights.
"""

from __future__ import annotations

import sys
import traceback
from typing import Optional

from PySide6.QtCore import QObject, QThread, Signal
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from gui import agent_io


class _SaveAndEnrollWorker(QObject):
    line = Signal(str)
    finished = Signal(bool, str)

    def __init__(
        self,
        *,
        backend_url: str,
        enrollment_token: str,
        verify_tls: bool,
        ca_bundle: Optional[str],
    ) -> None:
        super().__init__()
        self._backend_url = backend_url
        self._enrollment_token = enrollment_token
        self._verify_tls = verify_tls
        self._ca_bundle = ca_bundle

    def run(self) -> None:
        try:
            self.line.emit(f"Writing config to {agent_io.CONFIG_PATH}")
            if agent_io.STATE_PATH.exists():
                self.line.emit("Removing existing state.json (will re-enroll).")
                try:
                    agent_io.STATE_PATH.unlink()
                except OSError as err:
                    self.line.emit(f"Warning: {err}")

            agent_io.write_config(
                backend_url=self._backend_url,
                enrollment_token_plaintext=self._enrollment_token,
                verify_tls=self._verify_tls,
                ca_bundle=self._ca_bundle,
            )
            self.line.emit("Tightening ACL (SYSTEM + Administrators only).")
            agent_io.lock_down_acl(agent_io.CONFIG_PATH)

            if not agent_io.AGENT_EXE.exists():
                self.finished.emit(
                    False,
                    f"Agent binary not found at {agent_io.AGENT_EXE}. "
                    "Re-run the installer.",
                )
                return

            self.line.emit("")
            self.line.emit("Running enrollment (validates URL + token)...")
            ok = agent_io.run_agent_streaming(
                "--enroll-only", "--verbose", on_line=self.line.emit,
            )
            if not ok:
                self.finished.emit(
                    False,
                    "Enrollment failed -- inspect the log above. Common causes: "
                    "wrong token, backend unreachable, AGENT_ENROLLMENT_TOKEN "
                    "not set on backend.",
                )
                return
            self.finished.emit(
                True,
                "Configuration saved and host enrolled. The scheduled task "
                "will run on its next trigger.",
            )
        except PermissionError as err:
            self.finished.emit(
                False,
                f"Permission denied: {err}\n\n"
                "Launch the configurator as Administrator.",
            )
        except Exception as err:  # noqa: BLE001
            self.line.emit("")
            self.line.emit("ERROR: " + str(err))
            self.line.emit(traceback.format_exc())
            self.finished.emit(False, str(err))


class ConfiguratorWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("LanVentory Agent Configuration")
        self.resize(720, 600)

        central = QWidget()
        outer = QVBoxLayout(central)

        intro = QLabel(
            "Provide the backend URL and the fleet-wide enrollment token. "
            "These match <code>AGENT_ENROLLMENT_TOKEN</code> on the backend "
            "(<code>openssl rand -hex 32</code> at deployment time)."
        )
        intro.setWordWrap(True)
        intro.setTextFormat(2)
        outer.addWidget(intro)

        self.backend = QLineEdit()
        self.backend.setPlaceholderText("https://lanventory.example.com")

        self.token = QLineEdit()
        self.token.setEchoMode(QLineEdit.EchoMode.Password)
        self.token.setPlaceholderText("64-char hex enrollment token")

        self.show_token = QCheckBox("Show")
        self.show_token.toggled.connect(
            lambda on: self.token.setEchoMode(
                QLineEdit.EchoMode.Normal if on else QLineEdit.EchoMode.Password
            )
        )
        token_row = QHBoxLayout()
        token_row.addWidget(self.token, 1)
        token_row.addWidget(self.show_token)
        token_widget = QWidget()
        token_widget.setLayout(token_row)

        self.verify_tls = QCheckBox("Verify TLS certificate")
        self.verify_tls.setChecked(True)
        self.ca_bundle = QLineEdit()
        self.ca_bundle.setPlaceholderText("(optional) path to custom CA bundle .pem")

        form = QFormLayout()
        form.addRow("Backend URL:", self.backend)
        form.addRow("Enrollment token:", token_widget)
        form.addRow("", self.verify_tls)
        form.addRow("CA bundle:", self.ca_bundle)
        outer.addLayout(form)

        button_row = QHBoxLayout()
        self.save_btn = QPushButton("Save && enroll")
        self.save_btn.clicked.connect(self._save)
        self.save_btn.setDefault(True)
        button_row.addStretch(1)
        button_row.addWidget(self.save_btn)
        outer.addLayout(button_row)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setFont(QFont("Consolas", 9))
        outer.addWidget(self.log, stretch=1)

        self.setCentralWidget(central)
        self.setStatusBar(QStatusBar())

        if not agent_io.is_elevated():
            self.statusBar().showMessage(
                "Note: not elevated. 'Save & enroll' may fail; "
                "right-click -> Run as administrator if errors appear.",
            )

        self._thread: Optional[QThread] = None
        self._worker: Optional[_SaveAndEnrollWorker] = None

    def _save(self) -> None:
        url = (self.backend.text() or "").strip().rstrip("/")
        token = (self.token.text() or "").strip()
        if not url or not token:
            QMessageBox.warning(
                self, "Missing input",
                "Backend URL and enrollment token are required.",
            )
            return

        self.log.clear()
        self._set_busy(True)

        self._thread = QThread(self)
        self._worker = _SaveAndEnrollWorker(
            backend_url=url,
            enrollment_token=token,
            verify_tls=self.verify_tls.isChecked(),
            ca_bundle=(self.ca_bundle.text() or "").strip() or None,
        )
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.run)
        self._worker.line.connect(self._append)
        self._worker.finished.connect(self._on_finished)
        self._worker.finished.connect(self._thread.quit)
        self._thread.start()

    def _on_finished(self, ok: bool, summary: str) -> None:
        self._set_busy(False)
        self._append("")
        if ok:
            self._append("[OK] " + summary)
            QMessageBox.information(self, "Done", summary)
        else:
            self._append("[FAIL] " + summary)
            QMessageBox.warning(self, "Configuration failed", summary)

    def _append(self, line: str) -> None:
        self.log.appendPlainText(line)
        self.log.verticalScrollBar().setValue(
            self.log.verticalScrollBar().maximum()
        )

    def _set_busy(self, busy: bool) -> None:
        for w in (self.backend, self.token, self.verify_tls,
                  self.ca_bundle, self.save_btn):
            w.setEnabled(not busy)


def main() -> int:
    if sys.platform == "win32" and not agent_io.is_elevated():
        rc = agent_io.relaunch_elevated()
        if rc <= 32:
            QApplication(sys.argv)
            QMessageBox.critical(
                None,
                "Elevation required",
                "The configurator must run as Administrator. UAC was declined.",
            )
            return 1
        return 0

    app = QApplication(sys.argv)
    app.setApplicationName("LanVentory Agent Configuration")
    win = ConfiguratorWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
