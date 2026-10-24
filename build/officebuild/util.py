"""Process, file and logging helpers shared by the build steps."""

import hashlib
import os
import shlex
import shutil
import subprocess
import sys
from typing import Dict, List, Optional, Sequence


class BuildError(Exception):
    pass


def log(message: str) -> None:
    print("[office-build] " + message, flush=True)


def group(title: str) -> None:
    """A collapsible group in GitHub Actions logs, a heading elsewhere."""
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::endgroup::", flush=True)
        print("::group::" + title, flush=True)
    else:
        log("== " + title)


def end_group() -> None:
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::endgroup::", flush=True)


def quote(args: Sequence[str]) -> str:
    return " ".join(shlex.quote(str(arg)) for arg in args)


def run(
    args: Sequence[str],
    cwd: Optional[str] = None,
    env: Optional[Dict[str, str]] = None,
    check: bool = True,
    dry_run: bool = False,
    capture: bool = False,
) -> str:
    """Run a command, echoing it first. Returns its stdout when `capture` is set."""
    where = " (in %s)" % cwd if cwd else ""
    log("$ " + quote(args) + where)
    if dry_run:
        return ""
    merged = dict(os.environ)
    if env:
        merged.update(env)
    result = subprocess.run(
        [str(arg) for arg in args],
        cwd=cwd,
        env=merged,
        stdout=subprocess.PIPE if capture else None,
        text=True,
    )
    if check and result.returncode != 0:
        raise BuildError("command failed with exit code %d: %s" % (result.returncode, quote(args)))
    return result.stdout if capture else ""


def succeeds(args: Sequence[str], cwd: Optional[str] = None, dry_run: bool = False) -> bool:
    """Run a command that may fail; True when it exits 0."""
    log("$ " + quote(args) + (" (in %s)" % cwd if cwd else ""))
    if dry_run:
        return True
    return subprocess.run([str(arg) for arg in args], cwd=cwd).returncode == 0


def git(args: Sequence[str], cwd: str, dry_run: bool = False, capture: bool = False, check: bool = True) -> str:
    return run(["git"] + list(args), cwd=cwd, dry_run=dry_run, capture=capture, check=check)


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_sha256sums(directory: str, names: List[str]) -> str:
    """Write SHA256SUMS (sha256sum format) for `names` in `directory`."""
    path = os.path.join(directory, "SHA256SUMS")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        for name in sorted(names):
            handle.write("%s  %s\n" % (sha256_file(os.path.join(directory, name)), name))
    return path


def python() -> str:
    return sys.executable or "python3"


def require_tool(name: str, hint: str) -> str:
    found = shutil.which(name)
    if not found:
        raise BuildError("%s is not on PATH. %s" % (name, hint))
    return found


def repo_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
