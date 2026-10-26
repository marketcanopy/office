#!/usr/bin/env python3
#
# Market Canopy Office: deploy Document Builder beside x2t.
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
# ADR 0528 decision 3: Canopy's local Docs tools run on the document engine
# Office installs, started as a separate process (plugin-runtimes
# packages/office-bridge, P11). They need Document Builder's command-line
# `docbuilder` in the install's converter/ folder, beside x2t, doctrenderer
# and DoctRenderer.config, which it reads from its own folder.
#
# The desktop build already compiles it (build_tools sln.json lists
# core/DesktopEditor/doctrenderer/app_builder/docbuilder.pro for the desktop
# targets), but deploy_desktop.py copies only x2t. The hook copies it into
# every deployed converter/ folder after build_tools runs, so no patch line
# is spent on it.

import glob
import os
import shutil
import subprocess
import sys

# The libraries docbuilder links, as build_tools' mac_correct_rpath_docbuilder names them.
MAC_LIBRARIES = (
    "UnicodeConverter",
    "kernel",
    "kernel_network",
    "graphics",
    "PdfFile",
    "XpsFile",
    "OFDFile",
    "DjVuFile",
    "HtmlFile2",
    "Fb2File",
    "EpubFile",
    "IWorkFile",
    "HWPFile",
    "doctrenderer",
    "DocxRenderer",
)


class DocumentEngineError(Exception):
    pass


def converter_dirs(workspace, branding):
    """Every deployed converter/ folder: build_tools/out/<platform>/<branding>/<product>/converter."""
    return sorted(
        glob.glob(
            os.path.join(
                workspace, "build_tools", "out", "*", branding, "*", "converter"
            )
        )
    )


def platform_of(converter_dir):
    # .../out/<platform>/<branding>/<product>/converter
    return os.path.basename(
        os.path.dirname(os.path.dirname(os.path.dirname(converter_dir)))
    )


def built_docbuilder(workspace, branding, platform, executable):
    """Where the core build put docbuilder (deploy_desktop.py's core_build_dir/bin/<platform>)."""
    for build_dir in (
        os.path.join(workspace, "core", "build", branding),
        os.path.join(workspace, "core", "build"),
    ):
        path = os.path.join(build_dir, "bin", platform, executable)
        if os.path.isfile(path):
            return path
    return None


def check_linux_rpath(path, run):
    """docbuilder loads doctrenderer and the rest from its own folder: its RPATH or RUNPATH holds $ORIGIN."""
    readelf = shutil.which("readelf")
    if readelf is None:
        raise DocumentEngineError(
            "readelf is not on PATH; it checks docbuilder finds its libraries in converter/"
        )
    result = run([readelf, "-d", path], capture_output=True, text=True)
    if result.returncode != 0:
        raise DocumentEngineError("readelf could not read %s" % path)
    if not any(
        ("RPATH" in line or "RUNPATH" in line) and "$ORIGIN" in line
        for line in result.stdout.splitlines()
    ):
        raise DocumentEngineError(
            "%s has no $ORIGIN in its RPATH or RUNPATH; it would not find doctrenderer"
            % path
        )


def correct_mac_rpath(path, run):
    """What build_tools does for its own docbuilder: libraries through @rpath, and @rpath at the executable."""
    for library in MAC_LIBRARIES:
        name = "lib%s.dylib" % library
        run(
            ["install_name_tool", "-change", name, "@rpath/" + name, path],
            capture_output=True,
        )
    result = run(
        ["install_name_tool", "-add_rpath", "@executable_path", path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 and "would duplicate path" not in (result.stderr or ""):
        raise DocumentEngineError(
            "install_name_tool could not add @executable_path to %s" % path
        )


def deploy(workspace, branding, run=subprocess.run, host=sys.platform):
    """Copy docbuilder into every deployed converter/ folder; return the copies."""
    deployed = []
    for converter in converter_dirs(workspace, branding):
        windows = os.path.isfile(os.path.join(converter, "x2t.exe"))
        if not windows and not os.path.isfile(os.path.join(converter, "x2t")):
            continue
        executable = "docbuilder.exe" if windows else "docbuilder"
        platform = platform_of(converter)
        source = built_docbuilder(workspace, branding, platform, executable)
        if source is None:
            raise DocumentEngineError(
                "core/build/%s/bin/%s/%s is missing; the desktop build compiles docbuilder.pro"
                % (branding, platform, executable)
            )
        target = os.path.join(converter, executable)
        shutil.copy2(source, target)
        if platform.startswith("linux"):
            check_linux_rpath(target, run)
        elif platform.startswith("mac") and host == "darwin":
            correct_mac_rpath(target, run)
        deployed.append(target)
    if not deployed:
        raise DocumentEngineError(
            "no deployed converter/ folder with x2t under build_tools/out"
        )
    return deployed
