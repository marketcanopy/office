"""office-build: build every Market Canopy Office target, or one (ADR 0530).

    office-build --targets all|windows|macos|linux|<win-x64,win-arm64,mac-arm64,mac-x64,linux-x64,linux-arm64>
                 --flavor production|staging
                 [--package] [--sign] [--source-release] [--upstream-tag <tag>]

The host builds what it can and prints every requested target it skipped.
"""

import argparse
import json
import os
import sys
from typing import Dict, List

from . import measure, package, patches, source_release, thirdparty, toolchain, util
from . import targets as targetlib
from . import upstream as upstreamlib
from .workspace import Workspace

PHASES = ["prepare", "compile", "stage", "installers", "seal"]

# make.py up to and including its third-party step (make_common.make(): boost,
# ICU, OpenSSL, CEF, V8 and the rest), for a short run that proves a runner can
# build the dependencies before a full compile. Run with build_tools as cwd.
THIRD_PARTY_ONLY = """
import os, sys
root = os.getcwd()
for sub in ["scripts", "scripts/develop", "scripts/develop/vendor", "scripts/core_common",
            "scripts/core_common/modules", "scripts/core_common/modules/android"]:
    sys.path.append(os.path.join(root, sub))
import config, base, make_common
config.parse()
base.check_python()
base.set_env("BUILD_PLATFORM", config.option("platform"))
config.parse_defaults()
base.check_build_version(root)
base.configure_common_apps()
make_common.make()
"""


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="office-build", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--targets",
        default="all",
        help="all, windows, macos, linux, or a comma list of %s" % ", ".join(targetlib.TARGETS),
    )
    parser.add_argument("--flavor", choices=["production", "staging"], default="staging")
    parser.add_argument("--package", action="store_true", help="make installers after compiling")
    parser.add_argument(
        "--sign",
        action="store_true",
        help="signed release: macOS signs and notarizes here; Windows refuses to package unsigned "
        "binaries (the workflow signs them between --phase stage and --phase installers)",
    )
    parser.add_argument("--source-release", action="store_true", help="write the source archive and SBOM")
    parser.add_argument("--upstream-tag", help="build this upstream tag instead of UPSTREAM's (pins dropped)")
    parser.add_argument(
        "--release-number", type=int, default=0, help="<n> of <upstream version>-mc.<n>; 0 is a development build"
    )
    parser.add_argument(
        "--phase",
        choices=["all"] + PHASES,
        default="all",
        help="run one phase: the release workflow signs between stage and installers and between installers and seal",
    )
    parser.add_argument(
        "--work-dir",
        default=os.environ.get("OFFICE_WORK_DIR", ""),
        help="workspace outside any git checkout (default ../office-work beside this repository)",
    )
    parser.add_argument("--out", default="", help="artifact directory (default build/out/)")
    parser.add_argument("--install-deps", action="store_true", help="provision the host toolchain (CI runners)")
    parser.add_argument(
        "--third-party-only",
        action="store_true",
        help="compile only build_tools' third-party libraries (boost, ICU, OpenSSL, CEF, V8, ...) and stop",
    )
    parser.add_argument(
        "--check-patches", action="store_true", help="only check out the patched components and report the patch budget"
    )
    parser.add_argument(
        "--themes-from",
        default="",
        help="win-arm64 --phase stage: a win-x64 build's editors/sdkjs, for the theme previews an x64 host "
        "cannot generate for arm64",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the plan and commands, run nothing")
    return parser.parse_args(argv)


def phases_for(args: argparse.Namespace) -> List[str]:
    if args.phase != "all":
        return [args.phase]
    if args.check_patches:
        return ["prepare"]
    if args.third_party_only:
        return ["prepare", "compile"]
    return PHASES if args.package else ["prepare", "compile"]


def write_github_output(values: Dict[str, str]) -> None:
    path = os.environ.get("GITHUB_OUTPUT")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as handle:
        for key, value in values.items():
            handle.write("%s=%s\n" % (key, value))


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    source_root = util.repo_root()
    host = targetlib.detect_host()
    try:
        wanted = targetlib.parse_targets(args.targets)
        upstream = upstreamlib.load(source_root, args.upstream_tag)
    except (targetlib.TargetError, upstreamlib.UpstreamError) as error:
        print("office-build: " + str(error), file=sys.stderr)
        return 2

    build, skipped = targetlib.plan(host, wanted)
    util.log("host %s; upstream %s (%s)" % (host.describe(), upstream.tag, upstream.version))
    util.log("building: " + (", ".join(build) or "nothing"))
    for target, reason in skipped:
        util.log("SKIPPED %s: %s" % (target, reason))
    write_github_output({"built": ",".join(build), "skipped": ",".join(target for target, _ in skipped)})
    if not build and not args.source_release and not args.check_patches:
        util.log("no requested target can be built on this host")
        return 3 if wanted else 0

    version = "%s-mc.%d" % (upstream.version, args.release_number)
    work_dir = args.work_dir or os.path.join(os.path.dirname(source_root), "office-work")
    out_root = os.path.abspath(args.out or os.path.join(source_root, "build", "out"))
    workspace = Workspace(work_dir, upstream, args.dry_run)
    os.makedirs(workspace.root, exist_ok=True)
    monitor = measure.Monitor(workspace.root)
    phases = phases_for(args)
    platforms = [targetlib.build_tools_platform(target) for target in build]
    configure_base = [
        "--module",
        "desktop",
        "--platform",
        " ".join(platforms) or "native",
        "--update",
        "0",
        "--branch",
        upstream.branch,
        "--clean",
        "0",
    ]
    env = {
        "PRODUCT_VERSION": upstream.version,
        "BUILD_NUMBER": str(args.release_number),
        "OFFICE_VERSION": version,
        "OFFICE_UPSTREAM_TAG": upstream.tag,
        "OFFICE_FLAVOR": args.flavor,
        "OFFICE_BUILD_SIGNED": "1" if args.sign else "0",
        "OFFICE_SOURCE_ROOT": source_root,
        "OFFICE_PATCHES_APPLIED": "1",
        "OFFICE_OVERLAY_APPLIED": "1",
    }
    if not args.sign:
        util.log("unsigned: every artifact is labeled a development build and cannot enter a release manifest")
    status = "ok"
    applied: List[patches.Patch] = []
    try:
        repos: List[str] = []
        if "prepare" in phases or args.source_release or args.check_patches:
            monitor.begin("prepare")
            util.group("prepare: checkout at %s, branding, patches" % upstream.tag)
            repos = workspace.prepare(source_root, configure_base)
            unpinned = sorted(repo for repo in repos + ["build_tools"] if not upstream.commit(repo))
            if args.flavor == "production" and unpinned:
                raise util.BuildError(
                    "a production build needs every component pinned in UPSTREAM; unpinned: " + ", ".join(unpinned)
                )
            applied = patches.apply(source_root, workspace.root, args.dry_run)
            patches.apply_replacements(source_root, workspace.root, args.dry_run)
            patches.apply_additions(source_root, workspace.root, args.dry_run)
            util.end_group()
        elif os.path.isfile(os.path.join(workspace.root, "resolved-commits.json")):
            with open(os.path.join(workspace.root, "resolved-commits.json"), encoding="utf-8") as handle:
                workspace.resolved = json.load(handle)["commits"]
            applied = patches.inventory(source_root)
        if args.check_patches:
            monitor.end()
            return 0

        tools = toolchain.Toolchain()
        if build and ("compile" in phases or "installers" in phases or "stage" in phases):
            monitor.begin("toolchain")
            util.group("toolchain for %s" % host.describe())
            tools = toolchain.prepare(host, workspace.root, build, args.install_deps, args.dry_run)
            env.update(tools.env)
            util.end_group()

        if "compile" in phases and build:
            monitor.begin("compile")
            util.group("compile %s" % ", ".join(build))
            configure = configure_base + tools.configure_args
            if os.path.isdir(workspace.path("marketcanopy")):
                configure += ["--branding", "marketcanopy"]
            python = tools.python or util.python()
            thirdparty.drop_incomplete(workspace.root, platforms, args.dry_run)
            util.run([python, "configure.py"] + configure, cwd=workspace.build_tools, env=env, dry_run=args.dry_run)
            if args.third_party_only:
                util.run([python, "-c", THIRD_PARTY_ONLY], cwd=workspace.build_tools, env=env, dry_run=args.dry_run)
            else:
                util.run([python, "make.py"], cwd=workspace.build_tools, env=env, dry_run=args.dry_run)
            if not args.dry_run:
                thirdparty.check(workspace.root, platforms)
            util.end_group()

        source_info: Dict[str, str] = {}
        digests = {
            os.path.relpath(item.path, source_root).replace(os.sep, "/"): util.sha256_file(item.path)
            for item in applied
        }
        if args.source_release:
            monitor.begin("source-release")
            util.group("source release")
            if args.dry_run:
                util.log("would write %s/source/MarketCanopyOffice-%s-source.tar.gz" % (out_root, version))
            else:
                source_info = source_release.write(
                    source_root,
                    workspace.root,
                    repos or sorted(workspace.resolved),
                    out_root,
                    version,
                    upstream.tag,
                    workspace.resolved,
                    digests,
                )
            util.end_group()

        branded = os.path.isdir(workspace.path("marketcanopy"))
        for target in build:
            ctx = package.Context(
                workspace.root,
                out_root,
                target,
                version,
                upstream.version,
                str(args.release_number),
                branded,
                args.sign,
                args.dry_run,
                env,
                tools.python,
                args.themes_from if target == "win-arm64" else "",
            )
            if "stage" in phases:
                monitor.begin("stage " + target)
                util.group("stage " + target)
                staged = package.stage(ctx)
                write_github_output({"stage_dir": staged})
                util.end_group()
            if "installers" in phases:
                monitor.begin("installers " + target)
                util.group("installers " + target)
                package.installers(ctx)
                util.end_group()
            if "seal" in phases and not args.dry_run:
                # After the workflow has signed the installers: hashes of the final files.
                write_build_info(ctx, upstream.tag, workspace.resolved, digests, args.flavor, source_info, source_root)
        monitor.end()
    except util.BuildError as error:
        status = "failed"
        monitor.end("failed")
        util.end_group()
        print("office-build: " + str(error), file=sys.stderr)
        return 1
    finally:
        report = monitor.report(
            {
                "targets": build,
                "skipped": [target for target, _ in skipped],
                "phases_run": phases,
                "status": status,
                "upstream_tag": upstream.tag,
                "version": version,
            }
        )
        name = "measurements-%s-%s.json" % ("-".join(build) or "none", "-".join(phases))
        if not args.dry_run:
            measure.write_report(os.path.join(out_root, "measurements", name), report)
        util.log("measurements: " + json.dumps(report, sort_keys=True))
        summary = os.environ.get("GITHUB_STEP_SUMMARY")
        if summary:
            with open(summary, "a", encoding="utf-8") as handle:
                handle.write("### office-build %s (%s): %s\n\n" % (", ".join(build) or "-", ", ".join(phases), status))
                handle.write("| Wall | Build disk peak | Volume used peak / total | Memory peak / total |\n")
                handle.write("| --- | --- | --- | --- |\n")
                handle.write(
                    "| %ss | %s GiB | %s / %s GiB | %s / %s GiB |\n\n"
                    % (
                        report["wall_seconds"],
                        report["build_disk_peak_gib"],
                        report["work_volume_used_peak_gib"],
                        report["work_volume_total_gib"],
                        report["memory_used_peak_gib"],
                        report["host"]["memory_total_gib"],
                    )
                )
                for phase in report["phases"]:
                    handle.write("- %s: %ss (%s)\n" % (phase["phase"], phase["seconds"], phase["result"]))
                for target, reason in skipped:
                    handle.write("- SKIPPED %s: %s\n" % (target, reason))
                handle.write("\n")
    return 0


def write_build_info(
    ctx: package.Context,
    tag: str,
    commits: Dict[str, str],
    patch_digests: Dict[str, str],
    flavor: str,
    source_info: Dict[str, str],
    source_root: str,
) -> None:
    names = sorted(
        name
        for name in os.listdir(ctx.out_dir)
        if name.startswith(package.PRODUCT_SLUG + "-") and not name.endswith(".sparkle.json")
    )
    if not names:
        raise util.BuildError("no artifacts in %s to seal" % ctx.out_dir)
    files = {name: util.sha256_file(os.path.join(ctx.out_dir, name)) for name in names}
    office_commit = util.run(["git", "rev-parse", "HEAD"], cwd=source_root, capture=True).strip()
    info = {
        "target": ctx.target,
        "version": ctx.version,
        "flavor": flavor,
        "signed": ctx.sign,
        "label": "release candidate" if ctx.sign else "development build",
        "branded": ctx.branded,
        "office_commit": office_commit,
        "upstream": {"tag": tag, "commits": commits},
        "patches": patch_digests,
        "source_archive": source_info or None,
        "files": files,
    }
    with open(os.path.join(ctx.out_dir, "build-info.json"), "w", encoding="utf-8") as handle:
        json.dump(info, handle, indent=2, sort_keys=True)
        handle.write("\n")
    util.write_sha256sums(ctx.out_dir, names + ["build-info.json"])
    util.log("sealed %s: %s" % (ctx.target, ", ".join(names)))


def run() -> None:
    sys.exit(main(sys.argv[1:]))
