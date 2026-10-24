"""Per-host toolchain: the options build_tools needs on each OS and, with
--install-deps, the provisioning a clean runner lacks.

- Linux follows build_tools' own tools/linux/automate.py: its bundled Python,
  deps.py (apt packages, Node, Java), the prebuilt Qt 5.9.9 from
  ONLYOFFICE-data/build_tools_data, and the Ubuntu 16.04 sysroot (`--sysroot 1`,
  also the arm64 cross toolchain).
- Windows needs Visual Studio 2019 (the v142 toolset): build_tools' boost 1.72
  bootstraps with `vc142` and builds with `msvc-14.2`, which find only a 2019
  installation. Hosted windows-2025 has Visual Studio 2026, so --install-deps
  installs the 2019 Build Tools beside it. The one vswhere finds is passed as
  `--vs-path`; Qt 5.15.2 msvc2019_64 comes through aqtinstall, and upstream's
  tools/win/arm64/fetch_qt.py provides win-arm64's.
- macOS uses Xcode on the runner and Qt 5.15.2 clang_64 through aqtinstall; the
  editors are a Cocoa app, so Qt only provides qmake for core.
- Windows and macOS need CMake 3: the third-party CMakeLists build_tools builds
  (x265 for heif) set pre-3.5 policies to OLD, which CMake 4 refuses. Hosted
  images ship CMake 4, so --install-deps puts a pinned CMake 3 first on PATH.
  Linux gets CMake 3 from build_tools' own cmake.sh.
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
CMAKE_VERSION = "3.31.6"


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


def _cmake3(work_root: str, dry_run: bool) -> Dict[str, str]:
    """PATH with a CMake 3 from PyPI first, in its own venv so no other tool is shadowed."""
    venv = os.path.join(work_root, "cmake3")
    python = os.path.join(venv, "Scripts", "python.exe") if os.name == "nt" else os.path.join(venv, "bin", "python")
    if not os.path.isfile(python):
        util.run([util.python(), "-m", "venv", venv], dry_run=dry_run)
    util.run([python, "-m", "pip", "install", "--quiet", "cmake==" + CMAKE_VERSION], dry_run=dry_run)
    if dry_run:
        bin_dir = os.path.join(venv, "<cmake bin>")
    else:
        bin_dir = subprocess.run(
            [python, "-c", "import cmake; print(cmake.CMAKE_BIN_DIR)"], stdout=subprocess.PIPE, text=True, check=True
        ).stdout.strip()
    return {"PATH": bin_dir + os.pathsep + os.environ.get("PATH", "")}


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


VS2019_COMPONENTS = [
    "Microsoft.VisualStudio.Workload.VCTools",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "Microsoft.VisualStudio.Component.VC.Tools.ARM64",
    "Microsoft.VisualStudio.Component.VC.ATL",
    "Microsoft.VisualStudio.Component.VC.ATL.ARM64",
]


def _find_vs2019() -> str:
    vswhere = os.path.join(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        "Microsoft Visual Studio",
        "Installer",
        "vswhere.exe",
    )
    if not os.path.isfile(vswhere):
        return ""
    found = subprocess.run(
        [
            vswhere,
            "-version",
            "[16.0,17.0)",
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
    return found.splitlines()[0] if found else ""


def visual_studio(install_deps: bool, dry_run: bool) -> str:
    """vcvarsall.bat's folder of a Visual Studio 2019 with the x64 and ARM64 C++ tools."""
    if dry_run:
        return "<vswhere 2019>"
    found = _find_vs2019()
    if not found and install_deps:
        parameters = " ".join("--add " + name for name in VS2019_COMPONENTS) + " --includeRecommended --passive"
        package = ["choco", "install", "visualstudio2019buildtools", "--no-progress", "-y"]
        # A reboot request (3010) is not a failure; the vswhere query below decides.
        util.run(package + ["--package-parameters", parameters], check=False)
        found = _find_vs2019()
    if not found:
        raise util.BuildError(
            "no Visual Studio 2019 with the C++ x64 tools was found (build_tools' boost needs the v142 toolset); "
            "install the 2019 Build Tools or pass --install-deps"
        )
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
    vs_path = visual_studio(install_deps, dry_run)
    if install_deps and not shutil.which("iscc") and not os.environ.get("INNOPATH"):
        util.run(["choco", "install", "innosetup", "--no-progress", "-y"], dry_run=dry_run, check=False)
    env = _cmake3(work_root, dry_run) if install_deps else {}
    return Toolchain(["--qt-dir", qt_dir, "--vs-version", "2019", "--vs-path", vs_path], env, util.python())


def macos(work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    del build
    if not dry_run:
        util.require_tool("xcodebuild", "Install Xcode and select it with xcode-select.")
    qt_dir = os.environ.get("OFFICE_QT_DIR", "")
    if not qt_dir:
        if not install_deps:
            raise util.BuildError("set OFFICE_QT_DIR to a Qt %s with clang_64, or pass --install-deps" % QT_VERSION)
        qt_dir = _aqt(work_root, "mac", "clang_64", dry_run)
    env = _cmake3(work_root, dry_run) if install_deps else {}
    return Toolchain(["--qt-dir", qt_dir], env, util.python())


def prepare(host: targetlib.Host, work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    return {"linux": linux, "windows": windows, "macos": macos}[host.os](work_root, build, install_deps, dry_run)
