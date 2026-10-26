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
  Linux gets the CMake 3.30.0 build_tools' cmake.sh installs, Kitware's own
  build, unpacked in the work directory instead of /opt (no sudo). It links
  libstdc++ statically: build_tools runs cmake with the Ubuntu 16.04 sysroot's
  libraries on LD_LIBRARY_PATH, and a distribution cmake then fails to load
  ("GLIBCXX_3.4.32 not found").
- win-arm64 pins Windows SDK 10.0.22621, the one upstream's v8 build pins for
  ARM64: VS2019's ARM64 compiler does not declare `_CountOneBits64`, which the
  10.0.26100 headers on hosted runners call. --install-deps installs it (hosted
  images carry only 26100). vcvarsall picks the newest SDK unless told, so
  build_tools gets a vcvarsall that passes the version, and boost's b2, which
  runs vcvarsall on its own, gets it as its msvc `<setup>` through
  BOOST_BUILD_USER_CONFIG.
- Windows also needs Windows SDK 10.0.19041.0 with its Debugging Tools, for
  either target: v8 8.9's build/toolchain/win/setup_toolchain.py passes that
  version to vcvarsall, and build/vs_toolchain.py copies dbghelp.dll from its
  Debuggers folder. --install-deps installs it.
- Windows and macOS need grunt-cli for the web apps (build_js.py runs
  `grunt`); Linux gets it from build_tools' deps.py. --install-deps installs it.
- Every host needs Node.js 22.12 or later (the owner's floor is Node 22; the
  AI agent's Vite 7 build needs 22.12 within it) and Java 11 or later
  (build_tools' deps.py and develop/dependence.py); `check_requirements`
  fails the build before compiling when either is older or missing. The
  release workflow installs Node 22 with actions/setup-node.
- Windows needs Perl and the Python launcher, which hosted images carry and a
  bare Windows Server runner does not: OpenSSL's build runs `perl Configure`,
  and ICU's data build runs `py -3`. build_tools runs both through batch files
  whose errors it ignores, and creates their output folders first, so without
  them a run "passes" and every later run skips both libraries. --install-deps
  installs Strawberry Perl and Python (with its launcher) through Chocolatey.
  Strawberry Perl goes first on PATH unless the first perl is already a Windows
  build: under Git Bash it is MSYS perl, which OpenSSL's Configure refuses.
- Linux and macOS set DEPOT_TOOLS_DIR to v8's depot_tools as an absolute path.
  build_tools runs `./depot_tools/fetch`, which exports the relative
  `./depot_tools` for the scripts it calls, and bootstrap_python3 changes into
  that folder before running cipd, which then cannot find its digests.
"""

import os
import re
import shutil
import subprocess
import tarfile
import urllib.request
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

from . import targets as targetlib
from . import util

QT_VERSION = "5.15.2"
AQT_VERSION = "3.3.0"
CMAKE_VERSION = "3.31.6"
ARM64_WINDOWS_SDK = "10.0.22621.0"
ARM64_WINDOWS_SDK_PACKAGE = "windows-sdk-11-version-22H2-all"
V8_WINDOWS_SDK = "10.0.19041.0"
V8_WINDOWS_SDK_PACKAGE = "windows-sdk-10-version-2004-all"
# Where Chocolatey's strawberryperl puts perl.exe; the package's PATH change
# reaches only processes started after it.
STRAWBERRY_PERL_BIN = r"C:\Strawberry\perl\bin"
# Where Chocolatey's 7zip puts 7z.exe, which desktop-apps' make_zip.ps1 runs.
SEVEN_ZIP_DIR = r"C:\Program Files\7-Zip"
NODE_MIN = (22, 12)
JAVA_MIN_MAJOR = 11


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


def depot_tools(work_root: str) -> str:
    """v8's depot_tools checkout, where build_tools' v8_89 module clones it."""
    return os.path.join(os.path.abspath(work_root), "core", "Common", "3dParty", "v8_89", "depot_tools")


# What the desktop-apps/package Makefile runs (make_package.py), after deps.py.
LINUX_PACKAGING_PACKAGES = ["m4", "debhelper", "rpm", "fakeroot", "binutils-aarch64-linux-gnu"]

# What build_tools runs on Linux that deps.py leaves to the machine: GitHub's
# Ubuntu images have them, a minimal runner image (the Azure Container Apps
# runner) does not. python.sh and cmake.sh download with wget.
LINUX_BASE_PACKAGES = [
    "wget",
    "curl",
    "ca-certificates",
    "git",
    "xz-utils",
    "zip",
    "unzip",
    "file",
    "rsync",
    "patchelf",
    "pkg-config",
    "ninja-build",
    "gperf",
    "flex",
    "bison",
    "lsb-release",
]


# build_tools' tools/linux/cmake.sh downloads this version; the digest is
# Kitware's cmake-3.30.0-SHA-256.txt.
LINUX_CMAKE = "cmake-3.30.0-linux-x86_64"
LINUX_CMAKE_URL = "https://github.com/Kitware/CMake/releases/download/v3.30.0/%s.tar.gz" % LINUX_CMAKE
LINUX_CMAKE_SHA256 = "09846a3858583f38189b59177586adf125a08c15f3cddcaf7d7d7081ac86969f"


def _linux_cmake(work_root: str, dry_run: bool) -> str:
    """The bin folder of Kitware's CMake 3.30.0, unpacked in the work directory."""
    root = os.path.join(os.path.abspath(work_root), LINUX_CMAKE)
    bin_dir = os.path.join(root, "bin")
    if dry_run or os.path.isfile(os.path.join(bin_dir, "cmake")):
        return bin_dir
    archive = root + ".tar.gz"
    util.log("download " + LINUX_CMAKE_URL)
    with urllib.request.urlopen(LINUX_CMAKE_URL) as response, open(archive, "wb") as handle:
        shutil.copyfileobj(response, handle)
    digest = util.sha256_file(archive)
    if digest != LINUX_CMAKE_SHA256:
        os.remove(archive)
        raise util.BuildError("%s has sha256 %s, not %s" % (LINUX_CMAKE_URL, digest, LINUX_CMAKE_SHA256))
    with tarfile.open(archive) as tar:
        tar.extractall(os.path.dirname(root))
    os.remove(archive)
    return bin_dir


def linux(work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    tools = os.path.join(work_root, "build_tools", "tools", "linux")
    bundled = os.path.join(tools, "python3", "bin")
    deps_needed = install_deps and not os.path.isfile(os.path.join(tools, "packages_complete"))
    if deps_needed:
        # deps.py runs apt-get install without an update; a fresh runner's lists are stale.
        util.run(["sudo", "apt-get", "update"], dry_run=dry_run)
        util.run(["sudo", "apt-get", "install", "-y", "--no-install-recommends", *LINUX_BASE_PACKAGES], dry_run=dry_run)
    if not os.path.isdir(bundled):
        util.run(["bash", "./python.sh"], cwd=tools, dry_run=dry_run)
    python = os.path.join(bundled, "python3")
    if deps_needed:
        util.run([python, "./deps.py"], cwd=tools, dry_run=dry_run)
        # make_package.py's Makefile: m4 templates, debhelper for the .deb, rpmbuild for the .rpm
        # targets, and aarch64-linux-gnu-strip for both arm64 packages.
        util.run(
            ["sudo", "apt-get", "install", "-y", "--no-install-recommends", *LINUX_PACKAGING_PACKAGES], dry_run=dry_run
        )
    qt_dir = os.path.join(tools, "qt_build", "Qt-5.9.9")
    wanted = ["amd64"] + (["arm64"] if "linux-arm64" in build else [])
    if any(not os.path.isdir(os.path.join(qt_dir, {"amd64": "gcc_64", "arm64": "gcc_arm64"}[name])) for name in wanted):
        util.run([python, "./qt_binary_fetch.py", "all" if len(wanted) > 1 else wanted[0]], cwd=tools, dry_run=dry_run)
    path = bundled + os.pathsep + os.environ.get("PATH", "")
    if install_deps:
        path = _linux_cmake(work_root, dry_run) + os.pathsep + path
    env = {"PATH": path, "DEPOT_TOOLS_DIR": depot_tools(work_root)}
    return Toolchain(["--qt-dir", qt_dir, "--sysroot", "1"], env, python)


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


def _windows_kits() -> str:
    return os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Windows Kits", "10")


def _windows_sdk_installed(version: str) -> bool:
    return os.path.isfile(os.path.join(_windows_kits(), "Include", version, "um", "winsdkver.h"))


def _v8_sdk_missing(cpus: List[str]) -> List[str]:
    """What v8's Windows toolchain scripts need from the SDK and cannot find."""
    missing = [] if _windows_sdk_installed(V8_WINDOWS_SDK) else ["Windows SDK " + V8_WINDOWS_SDK]
    for cpu in cpus:
        if not os.path.isfile(os.path.join(_windows_kits(), "Debuggers", cpu, "dbghelp.dll")):
            missing.append("Debugging Tools for Windows (%s)" % cpu)
    return missing


def _choco(package: str, dry_run: bool) -> None:
    util.run(["choco", "install", package, "--no-progress", "-y"], dry_run=dry_run, check=False)


def _grunt(install_deps: bool, dry_run: bool) -> None:
    if install_deps and not shutil.which("grunt"):
        npm = shutil.which("npm") or "npm"
        util.run([npm, "install", "-g", "grunt-cli"], dry_run=dry_run)


def _windows_perl() -> bool:
    """Whether the first perl on PATH is a Windows build. Under `shell: bash` it
    is Git's MSYS perl, for which OpenSSL's Configure writes no makefile, and
    build_tools ignores the error."""
    return _tool_output(["perl", "-e", "print $^O"], {}).strip() == "MSWin32"


def windows_build_tools(install_deps: bool, dry_run: bool) -> List[str]:
    """Folders to put first on PATH so build_tools finds a Windows `perl`
    (OpenSSL) and `py` (ICU's data build), and make_zip.ps1 finds `7z`. A tool
    still missing fails the build here instead of hours later."""
    strawberry = os.path.isfile(os.path.join(STRAWBERRY_PERL_BIN, "perl.exe"))
    windows_perl = not dry_run and shutil.which("perl") and _windows_perl()
    if install_deps and not windows_perl and not strawberry:
        _choco("strawberryperl", dry_run)
    if install_deps and not shutil.which("py"):
        _choco("python3", dry_run)
    seven_zip = os.path.join(SEVEN_ZIP_DIR, "7z.exe")
    if install_deps and not shutil.which("7z") and not os.path.isfile(seven_zip):
        _choco("7zip", dry_run)
    if dry_run:
        return []
    extra = []
    if not windows_perl and os.path.isfile(os.path.join(STRAWBERRY_PERL_BIN, "perl.exe")):
        extra.append(STRAWBERRY_PERL_BIN)
    missing = []
    if not (windows_perl or extra):
        missing.append("a Windows Perl such as Strawberry Perl (OpenSSL runs `perl Configure`)")
    if not shutil.which("py"):
        missing.append("the Python launcher `py` (ICU's data build runs `py -3`)")
    if not shutil.which("7z"):
        if os.path.isfile(seven_zip):
            extra.append(SEVEN_ZIP_DIR)
        else:
            missing.append("7-Zip's `7z` (make_zip.ps1 runs it)")
    if missing:
        raise util.BuildError("%s not found; install it or pass --install-deps" % " and ".join(missing))
    return extra


def pin_windows_sdk(work_root: str, vs_path: str, version: str, dry_run: bool) -> Tuple[str, Dict[str, str]]:
    """A vcvarsall folder that passes `version` to the real one, and b2's user config that uses it."""
    folder = os.path.join(work_root, "vcvars-sdk")
    wrapper = os.path.join(folder, "vcvarsall.bat")
    user_config = os.path.join(folder, "user-config.jam")
    if not dry_run:
        if not _windows_sdk_installed(version):
            raise util.BuildError(
                "win-arm64 needs Windows SDK %s (Visual Studio 2019's ARM64 compiler cannot build the newer headers); "
                "install it or pass --install-deps" % version
            )
        os.makedirs(folder, exist_ok=True)
        with open(wrapper, "w", newline="\r\n") as handle:
            handle.write('@call "%s" %%* %s\n' % (os.path.join(vs_path, "vcvarsall.bat"), version))
        with open(user_config, "w") as handle:
            handle.write('using msvc : 14.2 : : <setup>"%s" ;\n' % wrapper.replace("\\", "\\\\"))
    return folder, {"BOOST_BUILD_USER_CONFIG": user_config}


def pin_icu_sdk(work_root: str, version: str, dry_run: bool) -> str:
    """MSBuild's Directory.Build.rsp above ICU's solution, so its ARM64 projects
    build against `version`: ICU names Windows SDK 10.0.16299.0 for ARM64, which
    neither Chocolatey nor the Visual Studio 2019 installer offers any longer.
    An empty `version` removes it, for a build without win-arm64."""
    path = os.path.join(work_root, "core", "Common", "3dParty", "icu", "Directory.Build.rsp")
    if dry_run:
        return path
    if not version:
        if os.path.isfile(path):
            os.remove(path)
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as handle:
        handle.write("/p:WindowsTargetPlatformVersion=%s\n" % version)
    return path


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
    # build_tools adds win_64 to a win_arm64 build on an x64 host.
    v8_cpus = ["x64"] + (["arm64"] if "win-arm64" in build else [])
    if install_deps and (dry_run or _v8_sdk_missing(v8_cpus)):
        _choco(V8_WINDOWS_SDK_PACKAGE, dry_run)
    if not dry_run and _v8_sdk_missing(v8_cpus):
        raise util.BuildError(
            "v8 needs %s (its setup_toolchain.py and vs_toolchain.py); install them or pass --install-deps"
            % " and ".join(_v8_sdk_missing(v8_cpus))
        )
    env: Dict[str, str] = {}
    if "win-arm64" in build:
        if install_deps and (dry_run or not _windows_sdk_installed(ARM64_WINDOWS_SDK)):
            _choco(ARM64_WINDOWS_SDK_PACKAGE, dry_run)
        vs_path, env = pin_windows_sdk(work_root, vs_path, ARM64_WINDOWS_SDK, dry_run)
    pin_icu_sdk(work_root, ARM64_WINDOWS_SDK if "win-arm64" in build else "", dry_run)
    if install_deps and not shutil.which("iscc") and not os.environ.get("INNOPATH"):
        _choco("innosetup", dry_run)
    _grunt(install_deps, dry_run)
    extra = windows_build_tools(install_deps, dry_run)
    if install_deps:
        env.update(_cmake3(work_root, dry_run))
    if extra:
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", os.environ.get("PATH", ""))])
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
    _grunt(install_deps, dry_run)
    env = _cmake3(work_root, dry_run) if install_deps else {}
    env["DEPOT_TOOLS_DIR"] = depot_tools(work_root)
    return Toolchain(["--qt-dir", qt_dir], env, util.python())


def major_version(output: str) -> int:
    """The major version in `node -v` or `java -version` output; Java 8 prints 1.8. 0 when there is none."""
    found = re.search(r'(?:^v|version ")(\d+)(?:\.(\d+))?', output, re.MULTILINE)
    if not found:
        return 0
    major = int(found.group(1))
    return int(found.group(2) or 0) if major == 1 else major


def node_version(output: str) -> Tuple[int, int]:
    """(major, minor) from `node -v` output; (0, 0) when there is none."""
    found = re.search(r"^v(\d+)\.(\d+)", output, re.MULTILINE)
    return (int(found.group(1)), int(found.group(2))) if found else (0, 0)


def _tool_output(args: List[str], env: Dict[str, str]) -> str:
    path = env.get("PATH", os.environ.get("PATH", ""))
    found = shutil.which(args[0], path=path)
    if not found:
        return ""
    result = subprocess.run(
        [found] + args[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False
    )
    return result.stdout


def check_requirements(env: Dict[str, str], dry_run: bool) -> None:
    """Fail before compiling when a tool build_tools needs is missing or too old, and log what was found."""
    if dry_run:
        return
    found = {
        "cmake": _tool_output(["cmake", "--version"], env),
        "node": _tool_output(["node", "-v"], env),
        "java": _tool_output(["java", "-version"], env),
        "git": _tool_output(["git", "--version"], env),
    }
    for name, output in found.items():
        util.log("%s: %s" % (name, (output.strip().splitlines() or ["not found"])[0]))
    problems = []
    if node_version(found["node"]) < NODE_MIN:
        problems.append("Node.js %d.%d or later" % NODE_MIN)
    if major_version(found["java"]) < JAVA_MIN_MAJOR:
        problems.append("Java %d or later" % JAVA_MIN_MAJOR)
    if problems:
        raise util.BuildError("build_tools needs " + " and ".join(problems) + " on PATH")


def prepare(host: targetlib.Host, work_root: str, build: List[str], install_deps: bool, dry_run: bool) -> Toolchain:
    tools = {"linux": linux, "windows": windows, "macos": macos}[host.os](work_root, build, install_deps, dry_run)
    check_requirements(tools.env, dry_run)
    return tools
