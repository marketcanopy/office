"""The UPSTREAM pin file: one upstream tag and the commit it resolved to in
each component (ADR 0529 decision 1).

Format, one `key=value` per line, `#` starts a comment:

    tag=v10.0.0.176
    version=10.0.0
    commit.core=76af764fcb777291098c73728044249b6893af6a
    commit.onlyoffice.github.io=<sha>
"""

import os
import re
from dataclasses import dataclass, field
from typing import Dict, Optional

SHA = re.compile(r"^[0-9a-f]{40}$")
TAG = re.compile(r"^v(\d+\.\d+\.\d+)\.\d+$")


class UpstreamError(Exception):
    pass


@dataclass
class Upstream:
    tag: str
    version: str
    commits: Dict[str, str] = field(default_factory=dict)

    @property
    def branch(self) -> str:
        """The value build_tools takes as --branch."""
        return "tags/" + self.tag

    def commit(self, repo: str) -> Optional[str]:
        return self.commits.get(repo)


def version_of_tag(tag: str) -> str:
    match = TAG.match(tag)
    if not match:
        raise UpstreamError("upstream tag %r is not of the form vX.Y.Z.N (for example v10.0.0.176)" % tag)
    return match.group(1)


def parse(text: str, source: str = "UPSTREAM") -> Upstream:
    values: Dict[str, str] = {}
    commits: Dict[str, str] = {}
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        key, sep, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if not sep or not key or not value:
            raise UpstreamError("%s:%d: expected key=value, got %r" % (source, number, raw))
        if key.startswith("commit."):
            repo = key[len("commit.") :]
            if not SHA.match(value):
                raise UpstreamError("%s:%d: %s is not a 40-character commit" % (source, number, key))
            commits[repo] = value
        else:
            values[key] = value
    if "tag" not in values:
        raise UpstreamError("%s: no tag= line" % source)
    tag = values["tag"]
    version = values.get("version") or version_of_tag(tag)
    if version != version_of_tag(tag):
        raise UpstreamError("%s: version=%s does not match tag %s" % (source, version, tag))
    return Upstream(tag=tag, version=version, commits=commits)


def load(repo_root: str, override_tag: Optional[str] = None) -> Upstream:
    """Read UPSTREAM; --upstream-tag replaces the tag and drops the commit pins,
    which then resolve from the tag (a sync rehearsal or a test of a new tag)."""
    path = os.path.join(repo_root, "UPSTREAM")
    if override_tag:
        return Upstream(tag=override_tag, version=version_of_tag(override_tag))
    if not os.path.isfile(path):
        raise UpstreamError("%s is missing. Pass --upstream-tag <tag> until the UPSTREAM file lands." % path)
    with open(path, encoding="utf-8") as handle:
        return parse(handle.read(), path)
