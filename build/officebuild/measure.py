"""Wall time per phase, peak disk and peak memory, sampled while the build runs.

P04 records these per target and runner class (ADR 0530 decision 3), so a
target that a hosted runner cannot finish is known by its numbers. Standard
library only: disk from shutil.disk_usage on the work volume, memory from
/proc/meminfo (Linux), vm_stat and sysctl (macOS) or GlobalMemoryStatusEx
(Windows).
"""

import json
import os
import platform
import shutil
import subprocess
import sys
import threading
import time
from typing import Dict, List, Optional, Tuple

GIB = 1024**3


def memory_bytes() -> Tuple[Optional[int], Optional[int]]:
    """(total, used) physical memory in bytes, or None where it cannot be read."""
    try:
        if sys.platform.startswith("linux"):
            info: Dict[str, int] = {}
            with open("/proc/meminfo", encoding="ascii") as handle:
                for line in handle:
                    key, _, rest = line.partition(":")
                    info[key] = int(rest.split()[0]) * 1024
            return info["MemTotal"], info["MemTotal"] - info["MemAvailable"]
        if sys.platform == "darwin":
            total = int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True))
            stats = subprocess.check_output(["vm_stat"], text=True)
            page = int(stats.split("page size of ")[1].split()[0])
            pages = {}
            for line in stats.splitlines()[1:]:
                key, _, value = line.partition(":")
                pages[key.strip()] = int(value.strip().rstrip("."))
            free = pages.get("Pages free", 0) + pages.get("Pages inactive", 0) + pages.get("Pages speculative", 0)
            return total, total - free * page
        if sys.platform.startswith("win"):
            import ctypes

            class MemoryStatus(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]

            status = MemoryStatus()
            status.dwLength = ctypes.sizeof(MemoryStatus)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
            return status.ullTotalPhys, status.ullTotalPhys - status.ullAvailPhys
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError):
        pass
    return None, None


class Monitor:
    """Samples disk and memory every `interval` seconds on a daemon thread."""

    def __init__(self, work_dir: str, interval: float = 30.0):
        self.work_dir = work_dir
        self.interval = interval
        self.started = time.time()
        usage = shutil.disk_usage(work_dir)
        self.disk_total = usage.total
        self.disk_used_start = usage.used
        self.disk_used_peak = usage.used
        self.memory_total, used = memory_bytes()
        self.memory_peak = used or 0
        self.phases: List[Dict[str, object]] = []
        self._phase_started: Optional[float] = None
        self._phase_name = ""
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._sample_loop, daemon=True)
        self._thread.start()

    def _sample(self) -> None:
        try:
            used = shutil.disk_usage(self.work_dir).used
            self.disk_used_peak = max(self.disk_used_peak, used)
        except OSError:
            pass
        _, memory = memory_bytes()
        if memory:
            self.memory_peak = max(self.memory_peak, memory)

    def _sample_loop(self) -> None:
        while not self._stop.wait(self.interval):
            self._sample()

    def begin(self, name: str) -> None:
        self.end()
        self._phase_name = name
        self._phase_started = time.time()

    def end(self, result: str = "ok") -> None:
        if self._phase_started is None:
            return
        self._sample()
        self.phases.append(
            {"phase": self._phase_name, "seconds": round(time.time() - self._phase_started), "result": result}
        )
        self._phase_started = None

    def report(self, extra: Dict[str, object]) -> Dict[str, object]:
        self._stop.set()
        self._sample()
        report: Dict[str, object] = {
            "host": {
                "system": platform.system(),
                "machine": platform.machine(),
                "cpus": os.cpu_count(),
                "memory_total_gib": round(self.memory_total / GIB, 1) if self.memory_total else None,
                "runner": os.environ.get("RUNNER_NAME"),
                "runner_environment": os.environ.get("RUNNER_ENVIRONMENT"),
                "image": os.environ.get("ImageOS"),
                "image_version": os.environ.get("ImageVersion"),
            },
            "work_volume_total_gib": round(self.disk_total / GIB, 1),
            "work_volume_used_start_gib": round(self.disk_used_start / GIB, 1),
            "work_volume_used_peak_gib": round(self.disk_used_peak / GIB, 1),
            "build_disk_peak_gib": round((self.disk_used_peak - self.disk_used_start) / GIB, 1),
            "memory_used_peak_gib": round(self.memory_peak / GIB, 1),
            "wall_seconds": round(time.time() - self.started),
            "phases": self.phases,
        }
        report.update(extra)
        return report


def write_report(path: str, report: Dict[str, object]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")
