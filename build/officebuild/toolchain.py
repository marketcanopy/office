"""Per-host toolchain: the options build_tools needs on each OS and, with
--install-deps, the provisioning a clean runner lacks.

- Linux follows build_tools' own tools/linux/automate.py: its bundled Python,
  deps.py (apt packages, Node, Java), the prebuilt Qt 5.9.9 from
  ONLYOFFICE-data/build_tools_data, and the Ubuntu 16.04 sysroot (`--sysroot 1`,
  also the arm64 cross toolchain).
- Windows uses the Visual Studio found by vswhere, passed as build_tools'
  `vs-version 2019` layout with an explicit `--vs-path` (hosted windows-2025 has
  only Visual Studio 2022), Qt 5.15.2 msvc2019_64 through aqtinstall, and
  upstream's tools/win/arm64/fetch_qt.py for win-arm64.
- macOS uses Xcode on the runner and Qt 5.15.2 clang_64 through aqtinstall; the
  editors are a Cocoa app, so Qt only provides qmake for core.
"""

import os
import shutil
import subprocess
from dataclasses import dataclass, field
from typing import Dict, List

from . import targets as targetlib
from . import util

QT_VERSION = "5.15.2"
AQT_VERSION = "3.3.0"


@dataclass
class Toolchain:
    configure_args: List[str] = field(default_factory=list)
    env: Dict[str, str] = field(default_factory=dict)
    python: str = ""


def _aqt(work_root: str, host: str, arch: str, dry_run: bool) -> str:
    qt_root = os.path.join(work_root, "qt")
    qt_dir = os.path.join(qt_root, QT_VERSION)
    folder = {"win64_msvc2019_64": "msvc2019_64", "clang_64": "clang_64"}[arch]
    if not os.path.isdir(os.path.join(qt_dir, folder)):
        util.run([util.python(), "-m", "pip", "install", "--quiet", "aqtinstall==" + AQT_VERSION], dry_run=dry_run)
        util.run(
            [util.python(), "-m", "aqt", "install-qt", host, "desktop", QT_VERSION, arch, "--outputdir", qt_root],
            dry_run=dry_run,
        )
    return qt_dir


def linux(work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    tools = os.path.join(work_root, "build_tools", "tools", "linux")
    bundled = os.path.join(tools, "python3", "bin")
    if not os.path.isdir(bundled):
        util.run(["bash", "./python.sh"], cwd=tools, dry_run=dry_run)
    python = os.path.join(bundled, "python3")
    if install_deps and not os.path.isfile(os.path.join(tools, "packages_complete")):
        # deps.py runs apt-get install without an update; a fresh runner's lists are stale.
        util.run(["sudo", "apt-get", "update"], dry_run=dry_run)
        util.run([python, "./deps.py"], cwd=tools, dry_run=dry_run)
        util.run(["sudo", "./cmake.sh"], cwd=tools, dry_run=dry_run)
        # make_package.py's Makefile: m4 templates, rpmbuild for the .rpm targets.
        util.run(
            ["sudo", "apt-get", "install", "-y", "--no-install-recommends", "m4", "rpm", "fakeroot"], dry_run=dry_run
        )
    qt_dir = os.path.join(tools, "qt_build", "Qt-5.9.9")
    wanted = ["amd64"] + (["arm64"] if "linux-arm64" in build else [])
    if any(not os.path.isdir(os.path.join(qt_dir, {"amd64": "gcc_64", "arm64": "gcc_arm64"}[name])) for name in wanted):
        util.run([python, "./qt_binary_fetch.py", "all" if len(wanted) > 1 else wanted[0]], cwd=tools, dry_run=dry_run)
    path = bundled + os.pathsep + os.environ.get("PATH", "")
    return Toolchain(["--qt-dir", qt_dir, "--sysroot", "1"], {"PATH": path}, python)


def visual_studio() -> str:
    vswhere = os.path.join(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        "Microsoft Visual Studio",
        "Installer",
        "vswhere.exe",
    )
    if not os.path.isfile(vswhere):
        raise util.BuildError("vswhere.exe not found; install Visual Studio 2019 or 2022 with C++ tools")
    found = subprocess.run(
        [
            vswhere,
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        stdout=subprocess.PIPE,
        text=True,
        check=False,
    ).stdout.strip()
    if not found:
        raise util.BuildError("no Visual Studio with the C++ x64 tools was found by vswhere")
    return os.path.join(found, "VC", "Auxiliary", "Build")


def windows(work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    qt_dir = os.environ.get("OFFICE_QT_DIR", "")
    if not qt_dir:
        if not install_deps:
            raise util.BuildError("set OFFICE_QT_DIR to a Qt %s with msvc2019_64, or pass --install-deps" % QT_VERSION)
        qt_dir = _aqt(work_root, "windows", "win64_msvc2019_64", dry_run)
    if "win-arm64" in build:
        arm_tools = os.path.join(work_root, "build_tools", "tools", "win", "arm64")
        if not os.path.isdir(os.path.join(arm_tools, "qt_build", "Qt-5.15.2")):
            util.run([util.python(), "fetch_qt.py"], cwd=arm_tools, dry_run=dry_run)
    vs_path = "<vswhere>" if dry_run else visual_studio()
    if install_deps and not shutil.which("iscc") and not os.environ.get("INNOPATH"):
        util.run(["choco", "install", "innosetup", "--no-progress", "-y"], dry_run=dry_run, check=False)
    return Toolchain(["--qt-dir", qt_dir, "--vs-version", "2019", "--vs-path", vs_path], {}, util.python())


def macos(work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    del build
    if not dry_run:
        util.require_tool("xcodebuild", "Install Xcode and select it with xcode-select.")
    qt_dir = os.environ.get("OFFICE_QT_DIR", "")
    if not qt_dir:
        if not install_deps:
            raise util.BuildError("set OFFICE_QT_DIR to a Qt %s with clang_64, or pass --install-deps" % QT_VERSION)
        qt_dir = _aqt(work_root, "mac", "clang_64", dry_run)
    return Toolchain(["--qt-dir", qt_dir], {}, util.python())


def prepare(host: targetlib.Host, work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    return {"linux": linux, "windows": windows, "macos": macos}[host.os](work_root, build, install_deps, dry_run)
