#!/usr/bin/env python3
#
# Market Canopy Office: build and bundle the AI plugins.
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
# ADR 0525 (amended 2026-10-26): both AI surfaces use Market Canopy as their
# default provider, beside upstream's built-in providers.
#
# The AI agent (desktop-sdk plugins/ai-agent) ships upstream as a prebuilt
# bundle in its deploy/ folder, which build_tools never copies. Its Market
# Canopy provider is source (the overlay and desktop-sdk patch 0003), so the
# hook builds the agent from source before build_tools runs and copies the
# bundle into every deployed editors/sdkjs-plugins folder afterwards.
#
# The in-editor AI plugin (onlyoffice.github.io content/ai) is deployed by
# build_tools from the branding's sdkjs-plugin list; the hook checks it was,
# with the Market Canopy provider listed first.

import glob
import json
import os
import shutil
import subprocess

AGENT_GUID = "{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}"
EDITOR_AI_GUID = "{9DC93CDB-B576-4F0C-B55E-FCC9C48DD007}"
AGENT_DIR = os.path.join("desktop-sdk", "ChromiumBasedEditors", "plugins", "ai-agent")
# Only the Market Canopy provider reads the native session.
AGENT_MARKER = "getMarketCanopyAiSession"
EDITOR_PROVIDERS = os.path.join("scripts", "engine", "providers")


class AiPluginError(Exception):
    pass


def agent_bundle(workspace):
    return os.path.join(workspace, AGENT_DIR, "deploy", AGENT_GUID)


def npm():
    found = shutil.which("npm")
    if not found:
        raise AiPluginError("npm is not on PATH; the AI agent is built from source with Node and npm")
    return found


def build_agent(workspace, run=subprocess.run):
    """npm ci (no lifecycle scripts) and npm run build in the agent, then
    check the bundle is ours and not upstream's prebuilt one."""
    folder = os.path.join(workspace, AGENT_DIR)
    if not os.path.isfile(os.path.join(folder, "package-lock.json")):
        raise AiPluginError("%s has no package-lock.json" % folder)
    command = npm()
    for args in (["ci", "--ignore-scripts", "--no-audit", "--no-fund"], ["run", "build"]):
        if run([command] + args, cwd=folder).returncode != 0:
            raise AiPluginError("npm %s failed in %s" % (" ".join(args), folder))
    check_agent_bundle(workspace)


def check_agent_bundle(workspace):
    index = os.path.join(agent_bundle(workspace), "index.js")
    if not os.path.isfile(index):
        raise AiPluginError("the AI agent build wrote no %s" % index)
    with open(index, "r", encoding="utf-8", errors="replace") as handle:
        if AGENT_MARKER not in handle.read():
            raise AiPluginError("%s is not built with the Market Canopy provider" % index)


def plugin_destinations(workspace, branding):
    """build_tools/out/<platform>/<branding>/<app>/editors/sdkjs-plugins."""
    pattern = os.path.join(workspace, "build_tools", "out", "*", branding, "*", "editors", "sdkjs-plugins")
    return sorted(path for path in glob.glob(pattern) if os.path.isdir(path))


def check_editor_plugin(destination):
    folder = os.path.join(destination, EDITOR_AI_GUID)
    config = os.path.join(folder, EDITOR_PROVIDERS, "config.json")
    if not os.path.isfile(config):
        raise AiPluginError(
            "the in-editor AI plugin is not deployed in %s (sdkjs-plugin in branding defaults)" % destination
        )
    with open(config, "r", encoding="utf-8") as handle:
        providers = json.load(handle)
    if not providers or providers[0] != "marketcanopy":
        raise AiPluginError("%s lists %s; it must list marketcanopy first" % (config, providers))
    if not os.path.isfile(os.path.join(folder, EDITOR_PROVIDERS, "internal", "marketcanopy.js")):
        raise AiPluginError("%s has no Market Canopy provider" % folder)


def deploy(workspace, branding):
    """Copy the built agent into every deployed editors/sdkjs-plugins and check
    the in-editor AI plugin there. Returns the destinations."""
    check_agent_bundle(workspace)
    destinations = plugin_destinations(workspace, branding)
    if not destinations:
        raise AiPluginError("build_tools deployed no editors/sdkjs-plugins for %s" % branding)
    for destination in destinations:
        target = os.path.join(destination, AGENT_GUID)
        if os.path.isdir(target):
            shutil.rmtree(target)
        shutil.copytree(agent_bundle(workspace), target)
        check_editor_plugin(destination)
    return destinations
