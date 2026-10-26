"""Library folders build_tools' third-party step leaves behind when a build it
ran failed.

On Windows, build_tools' OpenSSL module creates `openssl/build/<platform>`
before it runs `perl Configure` and nmake in a batch file whose errors it
ignores, its ICU module copies the MSBuild outputs with a copy that only
warns when a file is missing, and core's harfbuzz make.py writes harfbuzz.pri
only after a fresh clone. A failed build therefore leaves the folder that
the next run checks to skip the library, and the failure surfaces hours later
as a missing header in core. office-build removes such a folder before
build_tools runs, so the library is built again, and refuses a third-party
step that did not produce the files core includes and links.
"""

import os
import shutil
import stat
from typing import Dict, List

from . import util

ICU_MAJOR = "74"


def _windows_platforms(platforms: List[str]) -> List[str]:
    # build_tools adds win_64 to a win_arm64 build on an x64 host (config.py).
    wanted = [p for p in platforms if p.startswith("win_")]
    if "win_arm64" in wanted and "win_64" not in wanted:
        wanted.insert(0, "win_64")
    return wanted


def products(work_root: str, platforms: List[str]) -> Dict[str, List[str]]:
    """Each library output folder build_tools tests for, with the files it must hold."""
    third_party = os.path.join(work_root, "core", "Common", "3dParty")
    icu_header = os.path.join(third_party, "icu", "icu", "include", "unicode", "utypes.h")
    found: Dict[str, List[str]] = {}
    for platform in _windows_platforms(platforms):
        openssl = os.path.join(third_party, "openssl", "build", platform)
        found[openssl] = [
            os.path.join(openssl, "include", "openssl", "sha.h"),
            os.path.join(openssl, "lib", "libcrypto.lib"),
            os.path.join(openssl, "lib", "libssl.lib"),
        ]
        icu = os.path.join(third_party, "icu", platform, "build")
        names = ("icuuc.lib", "icudt.lib", "icuuc%s.dll" % ICU_MAJOR, "icudt%s.dll" % ICU_MAJOR)
        found[icu] = [os.path.join(icu, name) for name in names]
        # The headers are shared by every platform; the MSBuild that copies them
        # runs only when this folder is missing.
        found[icu].append(icu_header)
    if found:
        # Shared by every platform: its make.py clones the sources and writes the
        # qmake include only when the clone folder is missing.
        harfbuzz = os.path.join(third_party, "harfbuzz", "harfbuzz")
        found[harfbuzz] = [
            os.path.join(harfbuzz, "src", "hb.h"),
            os.path.join(third_party, "harfbuzz", "harfbuzz.pri"),
        ]
    return found


def missing(work_root: str, platforms: List[str]) -> List[str]:
    return [path for files in products(work_root, platforms).values() for path in files if not os.path.isfile(path)]


def _remove_tree(folder: str) -> None:
    # Git leaves its object files read-only, which Windows refuses to delete.
    def clear_read_only(function, path, _):
        os.chmod(path, stat.S_IWRITE)
        function(path)

    shutil.rmtree(folder, onerror=clear_read_only)


def drop_incomplete(work_root: str, platforms: List[str], dry_run: bool = False) -> List[str]:
    """Remove each output folder that exists without its files, so build_tools rebuilds it."""
    dropped = []
    for folder, files in products(work_root, platforms).items():
        if os.path.isdir(folder) and not all(os.path.isfile(path) for path in files):
            util.log("%s is incomplete (a failed build left it); removing it so build_tools rebuilds it" % folder)
            if not dry_run:
                _remove_tree(folder)
            dropped.append(folder)
    return dropped


def check(work_root: str, platforms: List[str]) -> None:
    absent = missing(work_root, platforms)
    if absent:
        raise util.BuildError(
            "build_tools' third-party step did not produce:\n  %s\nits build output above names why"
            % "\n  ".join(absent)
        )
