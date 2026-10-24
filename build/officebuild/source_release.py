"""The source release (ADR 0527): one archive of everything a build compiles from.

Contents of MarketCanopyOffice-<version>-source.tar.gz:

- `office/`: this repository at its commit (overlay, patch queue, build and
  sync scripts), from `git archive`;
- `upstream/<repo>/`: every ONLYOFFICE component at its UPSTREAM commit with the
  patch queue applied (`git archive` of the patched HEAD, submodules included);
- `SOURCE-MANIFEST.json`: commits, patch digests and the third-party libraries
  build_tools downloads during a build (not in the archive; listed with the
  module that fetches them);
- `SBOM.spdx.json`: an SPDX 2.3 document of the same.

Members keep `git archive`'s commit timestamps and the gzip header has no
time, so the same commits give the same archive digest. Each target's
build-info.json carries the same commits and patch digests, which is what
ties a binary to this archive.
"""

import gzip
import io
import json
import os
import tarfile
import tempfile
from typing import Dict, List

from . import util

# build_tools/scripts/core_common/make_common.py fetches these into
# core/Common/3dParty during a build.
THIRD_PARTY_MODULES = [
    "boost",
    "libxml2",
    "cef",
    "icu",
    "openssl",
    "v8",
    "html2",
    "iwork",
    "md",
    "hunspell",
    "harfbuzz",
    "glew",
    "hyphen",
    "googletest",
    "oo_brotli",
    "heif",
    "websocket_all",
]


def _git_archive(folder: str, prefix: str, scratch: str) -> List[str]:
    """Tar files of `folder` and its submodules, each under `prefix`."""
    outputs = []
    path = os.path.join(scratch, "%d.tar" % len(os.listdir(scratch)))
    util.run(["git", "archive", "--format=tar", "--prefix=" + prefix + "/", "-o", path, "HEAD"], cwd=folder)
    outputs.append(path)
    listing = util.run(["git", "submodule", "status", "--recursive"], cwd=folder, capture=True)
    for line in listing.splitlines():
        parts = line.strip().split()
        if len(parts) >= 2:
            sub = parts[1]
            outputs += _git_archive(os.path.join(folder, sub), prefix + "/" + sub, scratch)
    return outputs


def _dirty(folder: str) -> bool:
    return bool(util.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=folder, capture=True).strip())


def manifest(
    source_root: str, version: str, tag: str, commits: Dict[str, str], patch_digests: Dict[str, str]
) -> Dict[str, object]:
    office_commit = util.run(["git", "rev-parse", "HEAD"], cwd=source_root, capture=True).strip()
    return {
        "product": "Market Canopy Office",
        "version": version,
        "office": {"repository": "https://github.com/marketcanopy/office", "commit": office_commit},
        "upstream": {"tag": tag, "commits": commits},
        "patches": patch_digests,
        "third_party_fetched_at_build": [
            {"module": name, "fetched_by": "build_tools/scripts/core_common/modules/%s.py" % name}
            for name in THIRD_PARTY_MODULES
        ],
    }


def spdx(record: Dict[str, object]) -> Dict[str, object]:
    upstream = record["upstream"]
    packages = [
        {
            "SPDXID": "SPDXRef-office",
            "name": "marketcanopy-office",
            "versionInfo": record["version"],
            "downloadLocation": "git+https://github.com/marketcanopy/office@" + record["office"]["commit"],
            "licenseConcluded": "NOASSERTION",
            "licenseDeclared": "AGPL-3.0-only",
            "copyrightText": "NOASSERTION",
        }
    ]
    for repo, commit in sorted(upstream["commits"].items()):
        packages.append(
            {
                "SPDXID": "SPDXRef-upstream-" + repo.replace(".", "-"),
                "name": repo,
                "versionInfo": commit,
                "downloadLocation": "git+https://github.com/ONLYOFFICE/%s@%s" % (repo, commit),
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": "NOASSERTION",
                "copyrightText": "NOASSERTION",
            }
        )
    for item in record["third_party_fetched_at_build"]:
        packages.append(
            {
                "SPDXID": "SPDXRef-3rdparty-" + item["module"].replace("_", "-"),
                "name": item["module"],
                "versionInfo": "NOASSERTION",
                "downloadLocation": "NOASSERTION",
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": "NOASSERTION",
                "copyrightText": "NOASSERTION",
                "comment": "Downloaded during the build by " + item["fetched_by"],
            }
        )
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "Market Canopy Office %s source" % record["version"],
        "documentNamespace": "https://github.com/marketcanopy/office/spdx/%s/%s"
        % (record["version"], record["office"]["commit"]),
        "creationInfo": {"creators": ["Tool: office-build"], "created": "1970-01-01T00:00:00Z"},
        "packages": packages,
        "relationships": [
            {
                "spdxElementId": "SPDXRef-office",
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": package["SPDXID"],
            }
            for package in packages[1:]
        ],
    }


def _add_bytes(archive: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    archive.addfile(info, io.BytesIO(data))


def write(
    source_root: str,
    work_root: str,
    repos: List[str],
    out_root: str,
    version: str,
    tag: str,
    commits: Dict[str, str],
    patch_digests: Dict[str, str],
) -> Dict[str, str]:
    if _dirty(source_root):
        raise util.BuildError("the office checkout has uncommitted changes; a source release needs a commit")
    out_dir = os.path.join(out_root, "source")
    os.makedirs(out_dir, exist_ok=True)
    name = "MarketCanopyOffice-%s-source.tar.gz" % version
    path = os.path.join(out_dir, name)
    record = manifest(source_root, version, tag, commits, patch_digests)
    with tempfile.TemporaryDirectory() as scratch:
        parts = _git_archive(source_root, "office", scratch)
        for repo in sorted(repos):
            parts += _git_archive(os.path.join(work_root, repo), "upstream/" + repo, scratch)
        with open(path, "wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as zipped:
            with tarfile.open(fileobj=zipped, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for part in parts:
                    with tarfile.open(part) as source:
                        for member in source:
                            if member.name == "pax_global_header":
                                continue
                            archive.addfile(member, source.extractfile(member) if member.isfile() else None)
                _add_bytes(
                    archive, "SOURCE-MANIFEST.json", (json.dumps(record, indent=2, sort_keys=True) + "\n").encode()
                )
                _add_bytes(
                    archive, "SBOM.spdx.json", (json.dumps(spdx(record), indent=2, sort_keys=True) + "\n").encode()
                )
    with open(os.path.join(out_dir, "SOURCE-MANIFEST.json"), "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=2, sort_keys=True)
        handle.write("\n")
    with open(os.path.join(out_dir, "SBOM.spdx.json"), "w", encoding="utf-8") as handle:
        json.dump(spdx(record), handle, indent=2, sort_keys=True)
        handle.write("\n")
    util.write_sha256sums(out_dir, [name, "SOURCE-MANIFEST.json", "SBOM.spdx.json"])
    digest = util.sha256_file(path)
    util.log("source archive %s sha256 %s" % (name, digest))
    return {"name": name, "sha256": digest}
