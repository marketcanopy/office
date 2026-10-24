"""Targets, the host that can build each, and the build plan (ADR 0530 decision 1).

Each OS builds only its own targets: Windows builds win-x64 and cross-builds
win-arm64, macOS builds the macOS targets, Linux builds linux-x64 and
cross-builds linux-arm64 through upstream's sysroot. Anything else requested is
skipped and printed, never dropped silently.
"""

import platform
import sys
from dataclasses import dataclass
from typing import List, Tuple

# target -> (os, arch, build_tools platform, make_package.py platform)
TARGETS = {
    "win-x64": ("windows", "x64", "win_64", "windows_x64"),
    "win-arm64": ("windows", "arm64", "win_arm64", "windows_arm64"),
    "mac-arm64": ("macos", "arm64", "mac_arm64", "darwin_arm64"),
    "mac-x64": ("macos", "x64", "mac_64", "darwin_x86_64"),
    "linux-x64": ("linux", "x64", "linux_64", "linux_x86_64"),
    "linux-arm64": ("linux", "arm64", "linux_arm64", "linux_aarch64"),
}
GROUPS = {
    "all": list(TARGETS),
    "windows": ["win-x64", "win-arm64"],
    "macos": ["mac-arm64", "mac-x64"],
    "linux": ["linux-x64", "linux-arm64"],
}


class TargetError(Exception):
    pass


@dataclass(frozen=True)
class Host:
    os: str
    arch: str

    def describe(self) -> str:
        return "%s-%s" % (self.os, self.arch)


def detect_host() -> Host:
    system = sys.platform
    if system.startswith("win"):
        os_name = "windows"
    elif system == "darwin":
        os_name = "macos"
    elif system.startswith("linux"):
        os_name = "linux"
    else:
        os_name = system
    machine = platform.machine().lower()
    arch = "arm64" if machine in ("arm64", "aarch64") else "x64" if machine in ("x86_64", "amd64") else machine
    return Host(os_name, arch)


def parse_targets(value: str) -> List[str]:
    """`all`, `windows`, `macos`, `linux` or a comma list of target names."""
    wanted: List[str] = []
    for item in [part.strip() for part in value.split(",") if part.strip()]:
        if item in GROUPS:
            names = GROUPS[item]
        elif item in TARGETS:
            names = [item]
        else:
            raise TargetError("unknown target %r (use all, windows, macos, linux or %s)" % (item, ", ".join(TARGETS)))
        for name in names:
            if name not in wanted:
                wanted.append(name)
    if not wanted:
        raise TargetError("no target given")
    return wanted


def why_not(host: Host, target: str) -> str:
    """Empty when the host can build the target, otherwise the reason it cannot."""
    target_os, target_arch, _, _ = TARGETS[target]
    if host.os != target_os:
        return "needs a %s host (this host is %s)" % (target_os, host.describe())
    if target_os == "windows" and host.arch != "x64":
        return "Windows targets build on an x64 host (win-arm64 is cross-built there)"
    if target_os == "linux" and host.arch == "arm64" and target_arch == "x64":
        return "linux-x64 does not cross-build from an arm64 host"
    return ""


def plan(host: Host, wanted: List[str]) -> Tuple[List[str], List[Tuple[str, str]]]:
    build, skipped = [], []
    for target in wanted:
        reason = why_not(host, target)
        if reason:
            skipped.append((target, reason))
        else:
            build.append(target)
    return build, skipped


def os_of(target: str) -> str:
    return TARGETS[target][0]


def arch_of(target: str) -> str:
    return TARGETS[target][1]


def build_tools_platform(target: str) -> str:
    return TARGETS[target][2]


def package_platform(target: str) -> str:
    return TARGETS[target][3]
