#!/usr/bin/env python3
"""
Project C Day 7-8 host GUI.

PyQt6 + pyqtgraph operator panel for the ESP8266 motor controller.
The GUI intentionally keeps command traffic and telemetry traffic separate:
- commands: short text lines on TCP port 5005
- telemetry: fixed 20-byte binary frames on TCP port 5006
- socket reads: background QThread, never the Qt GUI thread
"""

import argparse
import socket
import struct
import sys
import time
from collections import deque
from dataclasses import dataclass

from PyQt6.QtCore import QThread, QTimer, Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QApplication,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QMainWindow,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)
import pyqtgraph as pg

CMD_PORT = 5005
TEL_PORT = 5006
FRAME = struct.Struct("<IhhhhBBHI")  # seq,target,actual,duty,current,state,fault,missed,uptime
PLOT_SECONDS = 30.0          # rolling plot window shown on both graphs
RECONNECT_SECONDS = 10.0       # Day 7-8 brief asks for 10 s retry

STATE = {0: "IDLE", 1: "ARMED", 2: "RUNNING", 3: "FAULT"}
STATE_COLOR = {0: "#777777", 1: "#8e44ad", 2: "#27ae60", 3: "#c0392b"}


def fault_text(flags: int) -> str:
    """Decode firmware fault bitmask into a readable label."""
    if flags == 0:
        return "NONE"
    names = []
    if flags & 0x01:
        names.append("OVERCURRENT")
    if flags & 0x02:
        names.append("STALL")
    if flags & 0x04:
        names.append("DRIVER")
    return "+".join(names) if names else f"0x{flags:02X}"


@dataclass
class Sample:
    seq: int
    target: int
    actual: int
    duty_permille: int
    current_ma: int
    state: int
    faults: int
    missed: int
    uptime_ms: int
    pc_time: float


def recv_exact(sock: socket.socket, nbytes: int) -> bytes:
    """TCP is a byte stream, so one recv() may not return a full frame."""
    data = bytearray()
    while len(data) < nbytes:
        part = sock.recv(nbytes - len(data))
        if not part:
            raise ConnectionError("telemetry socket closed")
        data.extend(part)
    return bytes(data)


class TelemetryThread(QThread):
    """Receive binary telemetry without blocking Qt widgets or button clicks."""

    sample_rx = pyqtSignal(object)
    status_rx = pyqtSignal(str)

    def __init__(self, host: str):
        super().__init__()
        self.host = host
        self._running = True

    def stop(self) -> None:
        self._running = False

    def run(self) -> None:
        while self._running:
            try:
                self.status_rx.emit(f"connecting telemetry {self.host}:{TEL_PORT}")
                with socket.create_connection((self.host, TEL_PORT), timeout=4.0) as sock:
                    sock.settimeout(2.0)
                    self.status_rx.emit("telemetry connected")

                    while self._running:
                        raw = recv_exact(sock, FRAME.size)
                        values = FRAME.unpack(raw)
                        self.sample_rx.emit(Sample(*values, pc_time=time.time()))

            except Exception as exc:
                if self._running:
                    self.status_rx.emit(f"telemetry disconnected: {exc}; retrying in 10 s")
                    for _ in range(int(RECONNECT_SECONDS * 10)):
                        if not self._running:
                            break
                        time.sleep(0.1)


class MainWindow(QMainWindow):
    def __init__(self, host: str):
        super().__init__()
        self.setWindowTitle("Project C Motor Controller - Day 7-8 GUI")
        self.samples = deque(maxlen=5000)
        self.last_seq = None
        self.gaps = 0
        self.last_state = None
        self.last_faults = None
        self.tel_thread = None

        self._build_ui(host)
        self._start_telemetry(host)

        # Draw at ~30 Hz. Telemetry arrives at 100 Hz; plotting every frame is wasted work.
        self.plot_timer = QTimer(self)
        self.plot_timer.timeout.connect(self._refresh_plot)
        self.plot_timer.start(33)

    def _build_ui(self, host: str) -> None:
        root = QWidget()
        self.setCentralWidget(root)
        layout = QGridLayout(root)

        # Connection row: IP can be edited if the router assigns a new address.
        self.host_edit = QLineEdit(host)
        reconnect_btn = QPushButton("Reconnect Telemetry")
        reconnect_btn.clicked.connect(lambda: self._start_telemetry(self.host_edit.text().strip()))
        layout.addWidget(QLabel("ESP IP:"), 0, 0)
        layout.addWidget(self.host_edit, 0, 1)
        layout.addWidget(reconnect_btn, 0, 2)

        # Command buttons: one click maps to one text command on port 5005.
        cmd_box = QGroupBox("Commands")
        cmd = QHBoxLayout(cmd_box)
        for label, text in [
            ("START / ARM", "ARM"),
            ("STOP", "STOP"),
            ("CLEAR_FAULT", "CLEAR_FAULT"),
            ("DISARM", "DISARM"),
            ("STATUS", "STATUS"),
        ]:
            b = QPushButton(label)
            b.clicked.connect(lambda _=False, t=text: self._send_command(t))
            cmd.addWidget(b)

        self.speed_spin = QSpinBox()
        self.speed_spin.setRange(0, 1500)
        self.speed_spin.setValue(300)
        send_speed = QPushButton("Send SET_SPEED")
        send_speed.clicked.connect(lambda: self._send_command(f"SET_SPEED {self.speed_spin.value()}"))
        cmd.addWidget(QLabel("RPM:"))
        cmd.addWidget(self.speed_spin)
        cmd.addWidget(send_speed)
        layout.addWidget(cmd_box, 1, 0, 1, 3)

        # Day 7-8 plots: target+actual RPM together, current below.
        self.rpm_plot = pg.PlotWidget(title="Target RPM and Actual RPM")
        self.rpm_plot.addLegend()
        self.rpm_plot.showGrid(x=True, y=True)
        self.target_curve = self.rpm_plot.plot(pen="y", name="target")
        self.actual_curve = self.rpm_plot.plot(pen="c", name="actual")

        self.current_plot = pg.PlotWidget(title="Current (mA)")
        self.current_plot.showGrid(x=True, y=True)
        self.current_curve = self.current_plot.plot(pen="m")
        layout.addWidget(self.rpm_plot, 2, 0, 1, 2)
        layout.addWidget(self.current_plot, 3, 0, 1, 2)

        # Colored state/fault panel and newest-first event history.
        side = QVBoxLayout()
        self.state_label = QLabel("STATE: --\nFAULT: --")
        self.state_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.state_label.setMinimumHeight(90)
        self.state_label.setStyleSheet("font-size: 22px; font-weight: bold; color: white; background: #777777;")
        self.values_label = QLabel("No telemetry yet")
        self.history = QListWidget()
        side.addWidget(self.state_label)
        side.addWidget(self.values_label)
        side.addWidget(QLabel("Fault / event history, newest first"))
        side.addWidget(self.history)
        layout.addLayout(side, 2, 2, 2, 1)

        self.statusBar().showMessage("GUI ready")
        self.resize(1200, 750)

    def _start_telemetry(self, host: str) -> None:
        if not host:
            self.statusBar().showMessage("Enter ESP IP first")
            return
        if self.tel_thread is not None:
            self.tel_thread.stop()
            self.tel_thread.wait(1500)
        self.samples.clear()
        self.last_seq = None
        self.tel_thread = TelemetryThread(host)
        self.tel_thread.sample_rx.connect(self._on_sample)
        self.tel_thread.status_rx.connect(self._on_status)
        self.tel_thread.start()

    def _send_command(self, cmd: str) -> None:
        """Send one text command to port 5005; firmware closes after one reply."""
        host = self.host_edit.text().strip()
        try:
            with socket.create_connection((host, CMD_PORT), timeout=2.0) as sock:
                sock.settimeout(2.0)
                sock.sendall((cmd + "\n").encode("ascii"))
                reply = sock.recv(512).decode("ascii", errors="replace").strip()
            self._add_event(f"CMD {cmd} -> {reply}")
            self.statusBar().showMessage(reply)
        except Exception as exc:
            self._add_event(f"CMD {cmd} failed: {exc}")
            self.statusBar().showMessage(f"command failed: {exc}")

    def _on_status(self, msg: str) -> None:
        self.statusBar().showMessage(msg)

        if "telemetry connected" in msg:
            # Reconnect downtime is logged as a reconnect event, not as a packet gap.
            # Sequence-gap counting restarts with the first frame of the new socket.
            self.last_seq = None

        if "disconnected" in msg or "connected" in msg:
            self._add_event(msg)

    def _on_sample(self, s: Sample) -> None:
        # Sequence-number check within one continuous telemetry connection.
        if self.last_seq is not None:
            expected = (self.last_seq + 1) & 0xFFFFFFFF
            if s.seq != expected and s.seq > self.last_seq:
                missed = s.seq - expected
                self.gaps += missed
                self._add_event(f"SEQ GAP +{missed}, total={self.gaps}")
        self.last_seq = s.seq
        self.samples.append(s)

        state_name = STATE.get(s.state, f"STATE{s.state}")
        fault_name = fault_text(s.faults)
        if s.state != self.last_state or s.faults != self.last_faults:
            self._add_event(f"state={state_name}, fault={fault_name}")
            self.last_state, self.last_faults = s.state, s.faults

        bg = STATE_COLOR.get(s.state, "#777777")
        self.state_label.setText(f"STATE: {state_name}\nFAULT: {fault_name}")
        self.state_label.setStyleSheet(f"font-size: 22px; font-weight: bold; color: white; background: {bg};")
        self.values_label.setText(
            f"seq={s.seq}  gaps={self.gaps}\n"
            f"target={s.target} rpm  actual={s.actual} rpm\n"
            f"duty={s.duty_permille / 10.0:.1f}%  current={s.current_ma} mA\n"
            f"missed_deadlines={s.missed}  uptime={s.uptime_ms} ms"
        )

    def _refresh_plot(self) -> None:
        if not self.samples:
            return
        now = self.samples[-1].pc_time
        recent = [s for s in self.samples if now - s.pc_time <= PLOT_SECONDS]
        t = [s.pc_time - now for s in recent]
        self.target_curve.setData(t, [s.target for s in recent])
        self.actual_curve.setData(t, [s.actual for s in recent])
        self.current_curve.setData(t, [s.current_ma for s in recent])

    def _add_event(self, text: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        self.history.insertItem(0, f"{stamp}  {text}")
        while self.history.count() > 10:
            self.history.takeItem(10)

    def closeEvent(self, event) -> None:
        if self.tel_thread is not None:
            self.tel_thread.stop()
            self.tel_thread.wait(1500)
        super().closeEvent(event)


def main() -> int:
    parser = argparse.ArgumentParser(description="Project C Day 7-8 GUI")
    parser.add_argument("--host", default="192.168.1.104", help="ESP8266 IP address printed in idf.py monitor")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    win = MainWindow(args.host)
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
