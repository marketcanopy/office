"""The build workspace: build_tools and every component at its UPSTREAM commit,
the branding folder staged beside them, and the patch queue applied.

build_tools resolves each component and the branding folder as siblings of its
own checkout (`<work>/build_tools/../<name>`), so that is the layout here. The
checkout is ours rather than build_tools' (`--update 1`), for three reasons:
every component is pinned to a recorded commit, including onlyoffice.github.io,
which has no release tag (P01 SOURCE-3); build_tools never runs git inside the
staged branding copy (P01 SOURCE-4); and the patched tree exists without a
build, for the source archive and the public patch check.
"""

import json
import os
import shutil
from typing import Dict, List, Optional

from . import util
from .upstream import Upstream

BRANDING = "marketcanopy"
ONLYOFFICE = "https://github.com/ONLYOFFICE/"

# Asked of build_tools itself, so the component set is upstream's for the
# configured modules, plus the branding's repositories_<module> defaults.
LIST_REPOSITORIES = """
import json, sys
sys.path.append("scripts")
import config, base
config.parse()
config.parse_defaults()
print("OFFICE_REPOSITORIES=" + json.dumps(sorted(base.get_repositories())))
"""


class Workspace:
    def __init__(self, work_dir: str, upstream: Upstream, dry_run: bool = False):
        self.root = os.path.abspath(work_dir)
        self.upstream = upstream
        self.dry_run = dry_run
        self.build_tools = os.path.join(self.root, "build_tools")
        self.resolved: Dict[str, str] = {}

    def path(self, repo: str) -> str:
        return os.path.join(self.root, repo)

    def check_location(self, source_root: str) -> None:
        """build_tools runs git in folders it expects to be checkouts; a work
        directory inside another work tree would let it walk up into that one."""
        probe = self.root
        while not os.path.isdir(probe):
            probe = os.path.dirname(probe)
        inside = util.run(["git", "-C", probe, "rev-parse", "--is-inside-work-tree"], check=False, capture=True).strip()
        if inside == "true":
            raise util.BuildError(
                "work directory %s is inside a git work tree; choose one outside it with --work-dir" % self.root
            )
        try:
            nested = os.path.commonpath([self.root, source_root]) == source_root
        except ValueError:  # different drives on Windows
            nested = False
        if nested:
            raise util.BuildError("work directory %s must not be inside %s" % (self.root, source_root))

    def checkout(self, repo: str) -> str:
        """Fetch `repo` at its pinned commit (or the tag when unpinned) and reset
        tracked files to it. Untracked build output stays, so warm builds keep
        their third-party libraries."""
        folder = self.path(repo)
        pin = self.upstream.commit(repo)
        ref = pin or "refs/tags/" + self.upstream.tag
        if not os.path.isdir(os.path.join(folder, ".git")):
            os.makedirs(folder, exist_ok=True)
            util.git(["init", "--quiet"], folder, self.dry_run)
            util.git(["remote", "add", "origin", ONLYOFFICE + repo + ".git"], folder, self.dry_run)
        if os.path.isdir(os.path.join(folder, ".git", "rebase-apply")):
            util.git(["am", "--abort"], folder, self.dry_run, check=False)
        fetch = ["git", "fetch", "--quiet", "--depth", "1", "--no-tags", "origin"]
        if not util.succeeds(fetch + [ref], folder, self.dry_run):
            if pin:
                raise util.BuildError("cannot fetch %s at its UPSTREAM commit %s" % (repo, pin))
            # As build_tools' git_update: a component without the tag builds
            # from its default branch (onlyoffice.github.io, P01 SOURCE-3).
            util.log("%s has no %s; falling back to its default branch, as build_tools does" % (repo, ref))
            util.git(fetch[1:] + ["HEAD"], folder)
        util.git(["-c", "advice.detachedHead=false", "checkout", "--quiet", "-f", "FETCH_HEAD"], folder, self.dry_run)
        util.git(["submodule", "update", "--init", "--recursive", "--depth", "1"], folder, self.dry_run)
        if self.dry_run:
            commit = pin or "<%s>" % ref
        else:
            commit = util.git(["rev-parse", "HEAD"], folder, capture=True).strip()
        if pin and commit != pin:
            raise util.BuildError("%s checked out %s, UPSTREAM pins %s" % (repo, commit, pin))
        if not pin:
            util.log("%s is not pinned in UPSTREAM; %s resolved to %s" % (repo, ref, commit))
        self.resolved[repo] = commit
        return commit

    def stage_branding(self, source_root: str) -> bool:
        """Copy branding/marketcanopy to <work>/marketcanopy. False when the
        repository has no branding folder yet (an unbranded build)."""
        source = os.path.join(source_root, "branding", BRANDING)
        target = self.path(BRANDING)
        if not os.path.isdir(source):
            util.log("branding/%s is not in this checkout: building UNBRANDED upstream" % BRANDING)
            return False
        util.log("staging %s -> %s" % (source, target))
        if not self.dry_run:
            if os.path.isdir(target):
                shutil.rmtree(target)
            shutil.copytree(source, target)
        return True

    def repositories(self, configure_args: List[str]) -> List[str]:
        """The component set build_tools would clone for this configuration."""
        util.run([util.python(), "configure.py"] + configure_args, cwd=self.build_tools, dry_run=self.dry_run)
        if self.dry_run:
            return [
                "core",
                "sdkjs",
                "sdkjs-forms",
                "onlyoffice.github.io",
                "web-apps",
                "dictionaries",
                "core-fonts",
                "desktop-sdk",
                "desktop-apps",
                "document-templates",
                "oforms.onlyoffice.com",
            ]
        output = util.run([util.python(), "-c", LIST_REPOSITORIES], cwd=self.build_tools, capture=True)
        for line in output.splitlines():
            if line.startswith("OFFICE_REPOSITORIES="):
                return json.loads(line.split("=", 1)[1])
        raise util.BuildError("build_tools did not report its repositories")

    def prepare(self, source_root: str, configure_args: List[str], only: Optional[List[str]] = None) -> List[str]:
        """Check out build_tools, stage branding, then every component."""
        os.makedirs(self.root, exist_ok=True)
        self.check_location(source_root)
        self.checkout("build_tools")
        branded = self.stage_branding(source_root)
        args = list(configure_args)
        if branded:
            args += ["--branding", BRANDING]
        repos = only if only is not None else self.repositories(args)
        for repo in repos:
            self.checkout(repo)
        self.write_resolved()
        return repos

    def write_resolved(self) -> None:
        if self.dry_run:
            return
        with open(os.path.join(self.root, "resolved-commits.json"), "w", encoding="utf-8") as handle:
            json.dump({"tag": self.upstream.tag, "commits": self.resolved}, handle, indent=2, sort_keys=True)
            handle.write("\n")
