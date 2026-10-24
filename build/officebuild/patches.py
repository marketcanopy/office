"""The patch queue: patches/<repo>/NNNN-<slug>.patch, one `git format-patch`
series per ONLYOFFICE component, applied with `git am --3way` in name order
(ADR 0529 decision 1). The commit dates are the patches' own, so a patched
HEAD is the same commit on every machine. office-build applies it right after its checkout and
exports OFFICE_PATCHES_APPLIED=1, so the branding hook does not apply it twice.

The budget (ADR 0529 decision 2) is at most 15 patches and 1,500 changed
upstream lines, new files excluded. It is reported, not enforced: exceeding it
needs the coordinator's recorded reason, not a failed build.
"""

import json
import os
import re
import shutil
from dataclasses import dataclass
from typing import Dict, List

from . import util

BUDGET_PATCHES = 15
BUDGET_LINES = 1500
IDENTITY = {
    "GIT_AUTHOR_NAME": "Market Canopy Office build",
    "GIT_AUTHOR_EMAIL": "office-build@marketcanopy.invalid",
    "GIT_COMMITTER_NAME": "Market Canopy Office build",
    "GIT_COMMITTER_EMAIL": "office-build@marketcanopy.invalid",
}


@dataclass
class Patch:
    repo: str
    path: str
    changed_lines: int  # added plus removed lines in files that exist upstream


def queue(source_root: str) -> Dict[str, List[str]]:
    """repo -> patch files in apply order."""
    root = os.path.join(source_root, "patches")
    result: Dict[str, List[str]] = {}
    if not os.path.isdir(root):
        return result
    for repo in sorted(os.listdir(root)):
        folder = os.path.join(root, repo)
        if not os.path.isdir(folder):
            continue
        files = sorted(name for name in os.listdir(folder) if name.endswith(".patch"))
        if files:
            result[repo] = [os.path.join(folder, name) for name in files]
    return result


def changed_upstream_lines(text: str) -> int:
    """Added plus removed lines, not counting files the patch creates."""
    total = 0
    counting = False
    for line in text.splitlines():
        if line.startswith("diff --git "):
            counting = True
        elif line.startswith("new file mode"):
            counting = False
        elif counting and re.match(r"^[+-](?![+-]{2} )", line):
            total += 1
    return total


def inventory(source_root: str) -> List[Patch]:
    patches = []
    for repo, files in queue(source_root).items():
        for path in files:
            with open(path, encoding="utf-8", errors="replace") as handle:
                patches.append(Patch(repo, path, changed_upstream_lines(handle.read())))
    return patches


def budget_line(patches: List[Patch]) -> str:
    lines = sum(patch.changed_lines for patch in patches)
    within = len(patches) <= BUDGET_PATCHES and lines <= BUDGET_LINES
    return "patch budget: %d of %d patches, %d of %d changed upstream lines (%s)" % (
        len(patches),
        BUDGET_PATCHES,
        lines,
        BUDGET_LINES,
        "within" if within else "OVER: needs a recorded reason",
    )


def apply(source_root: str, work_root: str, dry_run: bool = False) -> List[Patch]:
    """Apply the queue to the checked-out components in `work_root`."""
    patches = inventory(source_root)
    env = {key: os.environ.get(key, value) for key, value in IDENTITY.items()}
    for repo, files in queue(source_root).items():
        folder = os.path.join(work_root, repo)
        if not os.path.isdir(folder) and not dry_run:
            raise util.BuildError("patches/%s exists but %s is not checked out" % (repo, folder))
        util.run(
            ["git", "am", "--3way", "--keep-cr", "--committer-date-is-author-date"] + files,
            cwd=folder,
            env=env,
            check=False,
            dry_run=dry_run,
        )
        if not dry_run and os.path.isdir(os.path.join(folder, ".git", "rebase-apply")):
            util.run(["git", "am", "--abort"], cwd=folder, check=False)
            raise util.BuildError("patches/%s does not apply to %s at its UPSTREAM commit" % (repo, repo))
    util.log(budget_line(patches))
    return patches


def additions(source_root: str) -> List[str]:
    """branding/marketcanopy/add/<repo>/<path>: new files the build adds to a
    component, such as the account module (ADR 0524). Paths are relative to add/."""
    root = os.path.join(source_root, "branding", "marketcanopy", "add")
    found = []
    for current, _, names in os.walk(root):
        for name in names:
            found.append(os.path.relpath(os.path.join(current, name), root).replace(os.sep, "/"))
    return sorted(found)


def apply_additions(source_root: str, work_root: str, dry_run: bool = False) -> int:
    """Copy branding/marketcanopy/add/<repo>/<path> into the checkout. An
    addition never replaces an upstream file: one that upstream tracks is an
    error, so a change to upstream code stays in the patch queue where the
    budget counts it."""
    root = os.path.join(source_root, "branding", "marketcanopy", "add")
    files = additions(source_root)
    for relative in files:
        repo, _, inner = relative.partition("/")
        folder = os.path.join(work_root, repo)
        util.log("add %s" % relative)
        if dry_run:
            continue
        if not os.path.isdir(folder):
            raise util.BuildError("add/%s names %s, which is not checked out" % (relative, repo))
        if util.git(["ls-files", "--", inner], cwd=folder, capture=True).strip():
            raise util.BuildError("add/%s is an upstream file; change it with a patch" % relative)
        target = os.path.join(folder, inner)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(os.path.join(root, relative), target)
    return len(files)


def apply_replacements(source_root: str, work_root: str, dry_run: bool = False) -> int:
    """Copy branding/marketcanopy/replace/<repo>/<path> over the checkout: the
    binary replacements (icons, logos) P03 lists in replace/MANIFEST.json as
    path -> sha256. Exported as OFFICE_OVERLAY_APPLIED=1 afterwards."""
    root = os.path.join(source_root, "branding", "marketcanopy", "replace")
    manifest = os.path.join(root, "MANIFEST.json")
    if not os.path.isfile(manifest):
        return 0
    with open(manifest, encoding="utf-8") as handle:
        entries = json.load(handle)
    for relative, digest in sorted(entries.items()):
        source = os.path.join(root, relative)
        if util.sha256_file(source) != digest:
            raise util.BuildError("replace/%s does not match its MANIFEST.json digest" % relative)
        target = os.path.join(work_root, relative)
        util.log("replace %s" % relative)
        if not dry_run:
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(source, target)
    return len(entries)
