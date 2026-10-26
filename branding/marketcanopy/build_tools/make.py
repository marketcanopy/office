#!/usr/bin/env python3
#
# Market Canopy Office: build_tools branding hook.
# Copyright (C) 2026 Market Canopy LLC
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License version 3 as published by
# the Free Software Foundation, together with the additional terms in the
# LICENSE file at the root of this repository.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the implied
# warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# AGPL at https://www.gnu.org/licenses/agpl-3.0.html.
#
# SPDX-License-Identifier: AGPL-3.0-only
#
# build_tools' make.py runs this file in place of its own build when the
# branding folder (staged as <workspace>/marketcanopy) holds build_tools/make.py
# (ADR 0529 decision 1). It:
#
#   1. refuses a configuration that would let build_tools check components out
#      again (update=1), since that would discard the patch queue;
#   2. checks every component is at the commit UPSTREAM pins;
#   3. applies the patch queue, the replacement assets and the added files,
#      unless office-build already did (OFFICE_PATCHES_APPLIED=1,
#      OFFICE_OVERLAY_APPLIED=1);
#   4. sets the build-time settings (update feeds, help URL, launcher name,
#      editor publisher strings) and refuses upstream's update feeds;
#   5. writes the notices the About dialogs show (notices.py) and names the
#      macOS bundle in its localized InfoPlist.strings;
#   6. builds the AI agent from source (ai_plugins.py, ADR 0525);
#   7. runs upstream's make.py unchanged;
#   8. bundles the AI agent into the deployed editors and checks the in-editor
#      AI plugin is deployed with the Market Canopy provider;
#   9. deploys Document Builder beside x2t for Canopy's local Docs tools
#      (document_engine.py, ADR 0528).

import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys

import ai_plugins
import document_engine
import notices

HOOK_DIR = os.path.dirname(os.path.abspath(__file__))
BRANDING_DIR = os.path.dirname(HOOK_DIR)
WORKSPACE = os.path.dirname(BRANDING_DIR)
BUILD_TOOLS = os.path.join(WORKSPACE, "build_tools")
# Xcode targets whose localized InfoPlist.strings override CFBundleName.
MACOS_RESOURCES = (
    "desktop-apps/macos/ONLYOFFICE/Resources/ONLYOFFICE-arm",
    "desktop-apps/macos/ONLYOFFICE/Resources/ONLYOFFICE-x86_64",
)
UPSTREAM_BUNDLE_NAME = '"CFBundleName" = "ONLYOFFICE";'


def fail(message):
    sys.stderr.write("marketcanopy branding: " + message + "\n")
    sys.exit(1)


def log(message):
    print("[marketcanopy] " + message, flush=True)


def source_root():
    root = os.environ.get("OFFICE_SOURCE_ROOT", "")
    if root:
        return os.path.abspath(root)
    # Run in place: <repo>/branding/marketcanopy/build_tools/make.py
    candidate = os.path.dirname(os.path.dirname(BRANDING_DIR))
    if os.path.isfile(os.path.join(candidate, "UPSTREAM")):
        return candidate
    fail("set OFFICE_SOURCE_ROOT to the marketcanopy/office checkout (UPSTREAM and patches/ live there)")


def read_key_values(path):
    values = {}
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name, _, value = line.partition("=")
            values[name.strip()] = value.strip().strip("'\"")
    return values


def git(cwd, *args, capture=True, check=True):
    result = subprocess.run(
        ["git"] + list(args),
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if result.returncode != 0:
        if not check:
            return ""
        fail("git %s failed in %s: %s" % (" ".join(args), cwd, (result.stderr or "").strip()))
    return (result.stdout or "").strip()


def patch_files(root, repo):
    folder = os.path.join(root, "patches", repo)
    if not os.path.isdir(folder):
        return []
    return sorted(os.path.join(folder, name) for name in os.listdir(folder) if name.endswith(".patch"))


def check_and_patch_components(root, upstream):
    applied_by_office_build = os.environ.get("OFFICE_PATCHES_APPLIED") == "1"
    for key, pinned in sorted(upstream.items()):
        if not key.startswith("commit."):
            continue
        repo = key[len("commit.") :]
        if repo == "build_tools":
            path = BUILD_TOOLS
        else:
            path = os.path.join(WORKSPACE, repo)
        if not os.path.isdir(os.path.join(path, ".git")):
            fail("%s is not checked out at %s; office-build checks out every component at UPSTREAM" % (repo, path))
        patches = patch_files(root, repo)
        if subprocess.run(["git", "merge-base", "--is-ancestor", pinned, "HEAD"], cwd=path).returncode != 0:
            fail("%s is not at the pinned commit %s (UPSTREAM)" % (repo, pinned))
        ahead = int(git(path, "rev-list", "--count", pinned + "..HEAD"))
        if ahead == len(patches) and (ahead > 0 or not patches):
            continue
        if ahead != 0:
            fail(
                "%s has %d commits over %s but the queue holds %d patches; check it out again"
                % (repo, ahead, pinned, len(patches))
            )
        if applied_by_office_build:
            fail("OFFICE_PATCHES_APPLIED=1 but %s has none of its %d patches" % (repo, len(patches)))
        if git(path, "status", "--porcelain", "--untracked-files=no"):
            fail("%s has local changes; the queue applies only to a clean checkout" % repo)
        log("applying %d patch(es) to %s" % (len(patches), repo))
        git(
            path,
            "-c",
            "user.name=Market Canopy Office build",
            "-c",
            "user.email=office-build@marketcanopy.invalid",
            "am",
            "--3way",
            "--committer-date-is-author-date",
            *patches,
            capture=False,
        )


def apply_replacements(root):
    if os.environ.get("OFFICE_OVERLAY_APPLIED") == "1":
        return
    replace_dir = os.path.join(BRANDING_DIR, "replace")
    manifest_path = os.path.join(replace_dir, "MANIFEST.json")
    if not os.path.isfile(manifest_path):
        return
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    count = 0
    # The format office-build reads (build/officebuild/patches.py): path -> sha256.
    for relative, sha256 in sorted(manifest.items()):
        source = os.path.join(replace_dir, relative)
        with open(source, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        if digest != sha256:
            fail("replace/%s does not match MANIFEST.json (run scripts/make-artwork)" % relative)
        target = os.path.join(WORKSPACE, relative)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(source, target)
        count += 1
    log("copied %d replacement file(s)" % count)


def apply_additions():
    # branding/marketcanopy/add/<repo>/<path>: new files only, never an upstream one
    # (build/officebuild/patches.py apply_additions).
    if os.environ.get("OFFICE_OVERLAY_APPLIED") == "1":
        return
    add_dir = os.path.join(BRANDING_DIR, "add")
    count = 0
    for current, _, names in os.walk(add_dir):
        for name in sorted(names):
            source = os.path.join(current, name)
            relative = os.path.relpath(source, add_dir).replace(os.sep, "/")
            repo, _, inner = relative.partition("/")
            folder = os.path.join(WORKSPACE, repo)
            if git(folder, "ls-files", "--", inner):
                fail("add/%s is an upstream file; change it with a patch" % relative)
            target = os.path.join(folder, inner)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(source, target)
            count += 1
    log("added %d file(s)" % count)


def resolve_feeds(settings):
    flavor = os.environ.get("OFFICE_FLAVOR", "staging")
    if flavor not in ("staging", "production"):
        fail("OFFICE_FLAVOR must be staging or production, not %r" % flavor)
    feeds = settings["updates"][flavor]
    upstream_defaults = set(settings["updates"]["upstreamDefaults"])
    main = os.environ.get("DESKTOP_URL_UPDATES_MAIN_CHANNEL") or feeds["windowsAppcast"]
    dev = os.environ.get("DESKTOP_URL_UPDATES_DEV_CHANNEL") or feeds["windowsAppcastDev"]
    for name, value in (("main", main), ("dev", dev)):
        if not value:
            fail("no %s update feed for the %s flavor (settings.json updates.%s)" % (name, flavor, flavor))
        if value in upstream_defaults or "onlyoffice.com" in value:
            fail(
                "the %s update feed is upstream's (%s); Office must never update from ONLYOFFICE's feed" % (name, value)
            )
    os.environ["DESKTOP_URL_UPDATES_MAIN_CHANNEL"] = main
    os.environ["DESKTOP_URL_UPDATES_DEV_CHANNEL"] = dev
    # desktop-apps patch 0002 puts these in the macOS Info.plist (SUFeedURL,
    # SUPublicEDKey) through the Fastfile.
    for name, key in (("OFFICE_SPARKLE_FEED_ARM", "macosArmFeed"), ("OFFICE_SPARKLE_FEED_X86_64", "macosX64Feed")):
        value = os.environ.get(name) or feeds[key]
        if sys.platform == "darwin" and not value:
            fail("no %s for the %s flavor (settings.json updates.%s.%s)" % (name, flavor, flavor, key))
        if value in upstream_defaults or "onlyoffice.com" in value:
            fail("%s is upstream's feed (%s)" % (name, value))
        os.environ[name] = value
    if sys.platform == "darwin" and flavor == "production" and not os.environ.get("OFFICE_SPARKLE_PUBLIC_KEY"):
        fail("a production macOS build needs OFFICE_SPARKLE_PUBLIC_KEY (P12 owns the Sparkle key)")
    return flavor


def set_build_settings(settings):
    identity = settings["identity"]
    urls = settings["urls"]
    # build_tools patch 0001 reads these two; an empty help URL leaves the
    # editors on their bundled help instead of ONLYOFFICE's help site.
    os.environ["DESKTOP_URL_WEBAPPS_HELP"] = urls["help"]
    os.environ["DESKTOP_LAUNCHER_NAME"] = os.path.splitext(identity["windows"]["executable"])[0]
    # web-apps build/Gruntfile.js replaces these in the editors. Its defaults are
    # ONLYOFFICE's, and an empty value falls back to them, so every one is set.
    editor_strings = {
        "PUBLISHER_NAME": identity["copyrightHolder"],
        "PUBLISHER_URL": urls["site"],
        "COMPANY_NAME": identity["companyName"],
        "APP_TITLE_TEXT": identity["productName"],
        "SUPPORT_URL": urls["support"],
        "HELP_URL": urls["support"],
        "HELP_CENTER_WEB_EDITORS": urls["support"],
        "SUGGEST_URL": urls["support"],
        # web-apps patch 0001 takes the address, mail and phone rows out of
        # About; the editors still name a sales address in license warnings.
        "SALES_EMAIL": urls["supportMail"],
        "SUPPORT_EMAIL": urls["supportMail"],
    }
    for name, value in editor_strings.items():
        if not value:
            fail("settings.json leaves %s empty; web-apps would fall back to ONLYOFFICE's value" % name)
        os.environ[name] = value


def build_fields(root, upstream, flavor):
    commit = git(root, "rev-parse", "HEAD", check=False) or os.environ.get("OFFICE_SOURCE_COMMIT", "unknown")
    dates = git(root, "log", "--format=%cs", "--", "branding", "patches", "UPSTREAM", check=False).split()
    today = datetime.date.today().isoformat()
    return {
        "version": os.environ.get("OFFICE_VERSION") or upstream["version"] + "-mc.0",
        "number": os.environ.get("BUILD_NUMBER", "0"),
        "flavor": flavor,
        "upstreamTag": upstream["tag"],
        "sourceCommit": commit,
        # Additional term 2: the date or dates of modification.
        "modified": {"first": min(dates) if dates else today, "last": max(dates) if dates else today},
    }


def write_notices(build):
    for path in notices.write(WORKSPACE, BRANDING_DIR, build):
        log("wrote " + path)


def name_macos_bundle(settings):
    """142 localized InfoPlist.strings set CFBundleName to ONLYOFFICE; a patch
    would be 284 changed lines for one value, so the build rewrites it."""
    bundle_name = '"CFBundleName" = "%s";' % settings["identity"]["macos"]["bundleName"]
    count = 0
    for folder in MACOS_RESOURCES:
        root = os.path.join(WORKSPACE, folder)
        for current, _, files in os.walk(root):
            if "InfoPlist.strings" not in files:
                continue
            path = os.path.join(current, "InfoPlist.strings")
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
            if UPSTREAM_BUNDLE_NAME in text:
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(text.replace(UPSTREAM_BUNDLE_NAME, bundle_name))
                count += 1
    log("named the macOS bundle in %d InfoPlist.strings" % count)


def main():
    root = source_root()
    upstream = read_key_values(os.path.join(root, "UPSTREAM"))
    with open(os.path.join(BRANDING_DIR, "settings.json"), "r", encoding="utf-8") as handle:
        settings = json.load(handle)

    config = read_key_values(os.path.join(BUILD_TOOLS, "config"))
    if config.get("update") == "1":
        fail(
            "configure.py was run with --update 1; office-build checks components out at UPSTREAM and configures with --update 0"
        )
    branch = config.get("branch", "")
    if branch and branch != "tags/" + upstream["tag"]:
        fail("configured branch %s differs from UPSTREAM tag %s" % (branch, upstream["tag"]))
    if config.get("branding-name", "") not in ("", settings["identity"]["brandingName"]):
        fail("branding-name must be %s" % settings["identity"]["brandingName"])

    check_and_patch_components(root, upstream)
    apply_replacements(root)
    apply_additions()
    flavor = resolve_feeds(settings)
    set_build_settings(settings)
    write_notices(build_fields(root, upstream, flavor))
    name_macos_bundle(settings)

    branding = settings["identity"]["brandingName"]
    log("building the AI agent")
    try:
        ai_plugins.build_agent(WORKSPACE)
    except ai_plugins.AiPluginError as error:
        fail(str(error))

    log("flavor %s, upstream %s, running build_tools/make.py" % (flavor, upstream["tag"]))
    env = dict(os.environ, OO_RUNNING_BRANDING="1", OO_BRANDING=branding)
    code = subprocess.run([sys.executable, "make.py"] + sys.argv[1:], cwd=BUILD_TOOLS, env=env).returncode
    if code != 0:
        sys.exit(code)
    try:
        for destination in ai_plugins.deploy(WORKSPACE, branding):
            log("bundled the AI agent in " + destination)
    except ai_plugins.AiPluginError as error:
        fail(str(error))
    try:
        for destination in document_engine.deploy(WORKSPACE, branding):
            log("deployed Document Builder as " + destination)
    except document_engine.DocumentEngineError as error:
        fail(str(error))


if __name__ == "__main__":
    main()
