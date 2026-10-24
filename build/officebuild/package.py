"""Per-OS packages from the deployed tree in build_tools/out/<platform>/<branding>.

- Windows: desktop-apps/package make.ps1 (stage), then make_zip.ps1 and
  make_inno.ps1 (installers), called directly so the release workflow can sign
  every executable and DLL between the two, as platform-dashboard's
  desktop-release.yml does with azure/artifact-signing-action. Advanced
  Installer (MSI) is left out: it is a commercial tool the runners do not have.
- Linux: build_tools' make_package.py (desktop-apps/package Makefile): .deb,
  .rpm, SUSE .rpm and .tar.xz. Upstream has no AppImage target.
- macOS: xcodebuild of desktop-apps/macos (upstream's fastlane lane always
  signs and notarizes with ONLYOFFICE's setup), then, with --sign, the same
  Developer ID and App Store Connect API key notarization as desktop-release.yml,
  the Sparkle .zip and a .dmg.

Unsigned output is named and labeled a development build and cannot enter a
release manifest.
"""

import glob
import json
import os
import shutil
import subprocess
from dataclasses import dataclass, field
from typing import Dict, List

from . import targets as targetlib
from . import util
from .workspace import BRANDING

PRODUCT_SLUG = "MarketCanopyOffice"
# P02 contract section 2 identities for the branded build; upstream's otherwise.
BRANDED_WINDOWS = {"CompanyName": "MarketCanopy", "ProductName": "Office"}
UNBRANDED_WINDOWS = {"CompanyName": "ONLYOFFICE", "ProductName": "DesktopEditors"}
MAC_SCHEME = {"mac-arm64": "ONLYOFFICE-arm", "mac-x64": "ONLYOFFICE-x86_64"}


@dataclass
class Context:
    work_root: str
    out_root: str
    target: str
    version: str  # 10.0.0-mc.<n>
    product_version: str  # 10.0.0
    build_number: str  # <n>
    branded: bool
    sign: bool
    dry_run: bool
    env: Dict[str, str] = field(default_factory=dict)
    python: str = ""

    @property
    def out_dir(self) -> str:
        return os.path.join(self.out_root, self.target)

    def artifact_name(self, kind: str, extension: str) -> str:
        labels = "" if self.branded else "-unbranded"
        labels += "" if self.sign else "-development"
        middle = "-" + kind if kind else ""
        return "%s-%s-%s%s%s%s" % (PRODUCT_SLUG, self.version, self.target, middle, labels, extension)


def collect(ctx: Context, sources: List[str], kind: str, extension: str) -> List[str]:
    """Copy upstream's outputs into out/<target>/ under release names."""
    if ctx.dry_run:
        return [ctx.artifact_name(kind, extension)]
    if not sources:
        raise util.BuildError("packaging produced no %s for %s" % (extension, ctx.target))
    if len(sources) > 1:
        raise util.BuildError("expected one %s for %s, found %s" % (extension, ctx.target, sources))
    os.makedirs(ctx.out_dir, exist_ok=True)
    name = ctx.artifact_name(kind, extension)
    shutil.copyfile(sources[0], os.path.join(ctx.out_dir, name))
    util.log("artifact %s <- %s" % (name, sources[0]))
    return [name]


# Windows -------------------------------------------------------------------


def _windows_args(ctx: Context) -> List[str]:
    names = BRANDED_WINDOWS if ctx.branded else UNBRANDED_WINDOWS
    args = [
        "-Version",
        "%s.%s" % (ctx.product_version, ctx.build_number),
        "-Arch",
        targetlib.arch_of(ctx.target),
        "-Target",
        "opensource",
        "-CompanyName",
        names["CompanyName"],
        "-ProductName",
        names["ProductName"],
    ]
    return args


def _ps1(ctx: Context, script: str, args: List[str]) -> None:
    package_dir = os.path.join(ctx.work_root, "desktop-apps", "package")
    util.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script] + args,
        cwd=package_dir,
        env=ctx.env,
        dry_run=ctx.dry_run,
    )


def windows_stage_dir(ctx: Context) -> str:
    return os.path.join(ctx.work_root, "desktop-apps", "package", "build", targetlib.arch_of(ctx.target), "desktop")


def deployed_app_dir(ctx: Context) -> str:
    """build_tools/out/<platform>/<branding>/<app folder>: the folder deploy
    wrote the desktop editors to (it holds `editors/`)."""
    root = os.path.join(
        ctx.work_root,
        "build_tools",
        "out",
        targetlib.build_tools_platform(ctx.target),
        BRANDING if ctx.branded else "onlyoffice",
    )
    if ctx.dry_run:
        return os.path.join(root, "<app>")
    found = [path for path in glob.glob(os.path.join(root, "*")) if os.path.isdir(os.path.join(path, "editors"))]
    if len(found) != 1:
        raise util.BuildError("expected one deployed desktop app under %s, found %s" % (root, found))
    return found[0]


def windows_stage(ctx: Context) -> str:
    args = _windows_args(ctx) + [
        "-Branding",
        BRANDING if ctx.branded else "onlyoffice",
        "-SourceDir",
        deployed_app_dir(ctx),
    ]
    _ps1(ctx, "make.ps1", args)
    stage = windows_stage_dir(ctx)
    util.log("staged application: %s (sign every .exe and .dll here before installers)" % stage)
    return stage


def _assert_signed(ctx: Context, folder: str) -> None:
    """With --sign the workflow has signed the staged binaries; refuse to wrap
    unsigned ones in an installer."""
    script = (
        "$bad = Get-ChildItem -Recurse -Include *.exe,*.dll -Path '%s' | "
        "Where-Object { (Get-AuthenticodeSignature $_.FullName).Status -ne 'Valid' } | "
        "Select-Object -ExpandProperty FullName; "
        'if ($bad) { $bad | ForEach-Object { Write-Host "unsigned: $_" }; exit 1 }' % folder
    )
    util.run(["powershell", "-NoProfile", "-Command", script], dry_run=ctx.dry_run)


def windows_installers(ctx: Context) -> List[str]:
    stage = windows_stage_dir(ctx)
    if not ctx.dry_run and not os.path.isdir(stage):
        raise util.BuildError("%s is missing; run --phase stage first" % stage)
    if ctx.sign:
        _assert_signed(ctx, stage)
    args = _windows_args(ctx)
    branding_args = []
    if ctx.branded:
        branding_args = ["-BrandingDir", os.path.join(ctx.work_root, BRANDING, "desktop-apps", "package", "inno")]
    _ps1(ctx, "make_zip.ps1", args)
    _ps1(ctx, "make_inno.ps1", args + branding_args)
    package_dir = os.path.join(ctx.work_root, "desktop-apps", "package")
    names = collect(ctx, glob.glob(os.path.join(package_dir, "zip", "*.zip")), "", ".zip")
    names += collect(ctx, glob.glob(os.path.join(package_dir, "inno", "*.exe")), "setup", ".exe")
    return names


# Linux ---------------------------------------------------------------------


def linux_installers(ctx: Context) -> List[str]:
    args = [
        ctx.python or util.python(),
        "make_package.py",
        "-P",
        targetlib.package_platform(ctx.target),
        "-T",
        "desktop",
        "-V",
        ctx.product_version,
        "-B",
        ctx.build_number,
        "--package-edition",
        "opensource",
    ]
    if ctx.branded:
        args += ["-R", BRANDING]
    util.run(args, cwd=os.path.join(ctx.work_root, "build_tools"), env=ctx.env, dry_run=ctx.dry_run)
    package_dir = os.path.join(ctx.work_root, "desktop-apps", "package")
    names = collect(ctx, glob.glob(os.path.join(package_dir, "deb", "*.deb")), "", ".deb")
    names += collect(ctx, glob.glob(os.path.join(package_dir, "rpm", "build", "RPMS", "*", "*.rpm")), "", ".rpm")
    names += collect(
        ctx, glob.glob(os.path.join(package_dir, "rpm-suse", "build", "RPMS", "*", "*.rpm")), "suse", ".rpm"
    )
    tars = [path for path in glob.glob(os.path.join(package_dir, "tar", "*.tar.xz")) if "-help-" not in path]
    names += collect(ctx, tars, "", ".tar.xz")
    return names


# macOS ---------------------------------------------------------------------


def _mac_dirs(ctx: Context) -> Dict[str, str]:
    macos = os.path.join(ctx.work_root, "desktop-apps", "macos")
    return {
        "project": os.path.join(macos, "ONLYOFFICE.xcodeproj"),
        "macos": macos,
        "derived": os.path.join(ctx.work_root, "xcode", ctx.target),
        "stage": os.path.join(ctx.work_root, "stage", ctx.target),
        "plist": os.path.join(macos, "ONLYOFFICE", "Resources", MAC_SCHEME[ctx.target], "Info.plist"),
    }


def mac_app(ctx: Context) -> str:
    stage = _mac_dirs(ctx)["stage"]
    apps = glob.glob(os.path.join(stage, "*.app"))
    if ctx.dry_run:
        return os.path.join(stage, (PRODUCT_SLUG if ctx.branded else "ONLYOFFICE") + ".app")
    if len(apps) != 1:
        raise util.BuildError("expected one .app in %s, found %s" % (stage, apps))
    return apps[0]


def mac_stage(ctx: Context) -> str:
    dirs = _mac_dirs(ctx)
    scheme = MAC_SCHEME[ctx.target]
    plistbuddy = "/usr/libexec/PlistBuddy"
    for key, value in (
        ("CFBundleShortVersionString", ctx.product_version),
        ("CFBundleVersion", ctx.build_number),
        ("ASCBundleBuildNumber", ctx.build_number),
    ):
        util.run([plistbuddy, "-c", "Set :%s %s" % (key, value), dirs["plist"]], dry_run=ctx.dry_run)
    # As upstream's fastlane lane: the open-source license beside the app.
    license_dir = os.path.join(dirs["macos"], "Vendor", "ONLYOFFICE", "license")
    if not ctx.dry_run:
        os.makedirs(license_dir, exist_ok=True)
        shutil.copyfile(
            os.path.join(ctx.work_root, "desktop-apps", "package", "common", "license", "opensource", "LICENSE.html"),
            os.path.join(license_dir, "LICENSE.html"),
        )
    defines = ["$(inherited)", "USE_ADDON_MODE", "OO_BRANDING=" + (BRANDING if ctx.branded else "onlyoffice")]
    xcargs = [
        "OO_BRANDING=" + (BRANDING if ctx.branded else "onlyoffice"),
        "GCC_PREPROCESSOR_DEFINITIONS=" + " ".join(defines),
        "CODE_SIGN_IDENTITY=",
        "CODE_SIGNING_REQUIRED=NO",
        "CODE_SIGNING_ALLOWED=NO",
    ]
    for name, variable in (
        ("URL_APPCAST_UPDATES", "DESKTOP_URL_UPDATES_MAIN_CHANNEL"),
        ("URL_APPCAST_DEV_CHANNEL", "DESKTOP_URL_UPDATES_DEV_CHANNEL"),
    ):
        if os.environ.get(variable):
            xcargs.append("%s=%s" % (name, os.environ[variable]))
    util.run(
        [
            "xcodebuild",
            "-project",
            dirs["project"],
            "-scheme",
            scheme,
            "-configuration",
            "Release",
            "-derivedDataPath",
            dirs["derived"],
            "build",
        ]
        + xcargs,
        cwd=dirs["macos"],
        env=ctx.env,
        dry_run=ctx.dry_run,
    )
    built = glob.glob(os.path.join(dirs["derived"], "Build", "Products", "Release", "*.app"))
    if not ctx.dry_run:
        if len(built) != 1:
            raise util.BuildError("xcodebuild produced %s" % built)
        if os.path.isdir(dirs["stage"]):
            shutil.rmtree(dirs["stage"])
        os.makedirs(dirs["stage"])
        util.run(["ditto", built[0], os.path.join(dirs["stage"], os.path.basename(built[0]))])
    return dirs["stage"]


def _mach_o_files(app: str) -> List[str]:
    """Nested code, deepest first, so each container is signed after its contents."""
    found = []
    for root, dirs, files in os.walk(app):
        for name in dirs:
            if name.endswith((".app", ".framework", ".xpc", ".bundle", ".appex")):
                found.append(os.path.join(root, name))
        for name in files:
            path = os.path.join(root, name)
            if os.path.islink(path):
                continue
            with open(path, "rb") as handle:
                magic = handle.read(4)
            if magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xce\xfa\xed\xfe", b"\xbe\xba\xfe\xca"):
                found.append(path)
    return sorted(set(found), key=lambda path: path.count(os.sep), reverse=True)


def _codesign(ctx: Context, path: str, entitlements: str = "") -> None:
    args = [
        "codesign",
        "--force",
        "--timestamp",
        "--options",
        "runtime",
        "--sign",
        os.environ["APPLE_SIGNING_IDENTITY"],
    ]
    if os.environ.get("MACOS_KEYCHAIN_PATH"):
        args += ["--keychain", os.environ["MACOS_KEYCHAIN_PATH"]]
    if entitlements:
        args += ["--entitlements", entitlements]
    util.run(args + [path], dry_run=ctx.dry_run)


def _notarize(ctx: Context, path: str) -> None:
    util.run(
        [
            "xcrun",
            "notarytool",
            "submit",
            path,
            "--key",
            os.environ["APPLE_API_KEY_PATH"],
            "--key-id",
            os.environ["APPLE_API_KEY_ID"],
            "--issuer",
            os.environ["APPLE_API_ISSUER"],
            "--wait",
        ],
        dry_run=ctx.dry_run,
    )


def mac_installers(ctx: Context) -> List[str]:
    dirs = _mac_dirs(ctx)
    app = mac_app(ctx)
    os.makedirs(ctx.out_dir, exist_ok=True)
    if ctx.sign:
        missing = [
            name
            for name in ("APPLE_SIGNING_IDENTITY", "APPLE_API_KEY_PATH", "APPLE_API_KEY_ID", "APPLE_API_ISSUER")
            if not os.environ.get(name)
        ]
        if missing:
            raise util.BuildError("--sign on macOS needs %s" % ", ".join(missing))
        entitlements = os.path.join(
            dirs["macos"], "ONLYOFFICE", "Resources", MAC_SCHEME[ctx.target], "ONLYOFFICE.entitlements"
        )
        for path in [] if ctx.dry_run else _mach_o_files(app):
            _codesign(ctx, path)
        _codesign(ctx, app, entitlements)
        util.run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", app], dry_run=ctx.dry_run)
        submission = os.path.join(ctx.work_root, "stage", ctx.target + "-notarize.zip")
        util.run(["ditto", "-c", "-k", "--keepParent", app, submission], dry_run=ctx.dry_run)
        _notarize(ctx, submission)
        util.run(["xcrun", "stapler", "staple", app], dry_run=ctx.dry_run)
    zip_name = ctx.artifact_name("", ".zip")
    util.run(
        ["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, os.path.join(ctx.out_dir, zip_name)],
        dry_run=ctx.dry_run,
    )
    dmg_name = ctx.artifact_name("", ".dmg")
    dmg_root = os.path.join(ctx.work_root, "stage", ctx.target + "-dmg")
    if not ctx.dry_run:
        if os.path.isdir(dmg_root):
            shutil.rmtree(dmg_root)
        os.makedirs(dmg_root)
        util.run(["ditto", app, os.path.join(dmg_root, os.path.basename(app))])
        os.symlink("/Applications", os.path.join(dmg_root, "Applications"))
    dmg = os.path.join(ctx.out_dir, dmg_name)
    util.run(
        [
            "hdiutil",
            "create",
            "-volname",
            "Market Canopy Office",
            "-srcfolder",
            dmg_root,
            "-ov",
            "-format",
            "UDZO",
            dmg,
        ],
        dry_run=ctx.dry_run,
    )
    if ctx.sign:
        _codesign(ctx, dmg)
        _notarize(ctx, dmg)
        util.run(["xcrun", "stapler", "staple", dmg], dry_run=ctx.dry_run)
        sparkle_key = os.environ.get("OFFICE_SPARKLE_ED_PRIVATE_KEY", "")
        if sparkle_key and not ctx.dry_run:
            sign_update = os.path.join(dirs["macos"], "Vendor", "Sparkle", "bin", "sign_update")
            signature = subprocess.run(
                [sign_update, "--ed-key-file", "-", os.path.join(ctx.out_dir, zip_name)],
                input=sparkle_key,
                stdout=subprocess.PIPE,
                text=True,
                check=True,
            ).stdout
            with open(os.path.join(ctx.out_dir, zip_name + ".sparkle.json"), "w", encoding="utf-8") as handle:
                json.dump({"archive": zip_name, "attributes": signature.strip()}, handle, indent=2)
                handle.write("\n")
        elif not sparkle_key:
            util.log("OFFICE_SPARKLE_ED_PRIVATE_KEY is not set: the Sparkle archive has no EdDSA signature")
    return [zip_name, dmg_name]


def stage(ctx: Context) -> str:
    os_name = targetlib.os_of(ctx.target)
    if os_name == "windows":
        return windows_stage(ctx)
    if os_name == "macos":
        return mac_stage(ctx)
    return ""  # Linux packages straight from the deployed tree


def installers(ctx: Context) -> List[str]:
    os_name = targetlib.os_of(ctx.target)
    return {"windows": windows_installers, "macos": mac_installers, "linux": linux_installers}[os_name](ctx)
