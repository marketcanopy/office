import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from officebuild import package

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "branding", "marketcanopy", "build_tools"))

import ai_plugins  # noqa: E402


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


class AiPlugins(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.workspace = self.tmp.name
        self.agent = os.path.join(self.workspace, ai_plugins.AGENT_DIR)
        write(os.path.join(self.agent, "package-lock.json"), "{}")

    def tearDown(self):
        self.tmp.cleanup()

    def built(self, marker=True):
        bundle = ai_plugins.agent_bundle(self.workspace)
        write(os.path.join(bundle, "index.js"), "x.getMarketCanopyAiSession()" if marker else "upstream")
        write(os.path.join(bundle, "config.json"), "{}")

    def deployed(self, platform, providers=("marketcanopy",), provider_file=True):
        plugins = os.path.join(
            self.workspace, "build_tools", "out", platform, "marketcanopy", "desktopeditors", "editors", "sdkjs-plugins"
        )
        engine = os.path.join(plugins, ai_plugins.EDITOR_AI_GUID, ai_plugins.EDITOR_PROVIDERS)
        write(os.path.join(engine, "config.json"), json.dumps(list(providers)))
        if provider_file:
            write(os.path.join(engine, "internal", "marketcanopy.js"), "// provider")
        return plugins

    def test_build_runs_npm_ci_without_scripts_then_build(self):
        calls = []

        def run(args, cwd):
            calls.append((args[1:], cwd))
            if args[1:] == ["run", "build"]:
                self.built()
            return subprocess.CompletedProcess(args, 0)

        with mock.patch.object(ai_plugins.shutil, "which", return_value="/usr/bin/npm"):
            ai_plugins.build_agent(self.workspace, run=run)
        self.assertEqual(
            calls,
            [
                (["ci", "--ignore-scripts", "--no-audit", "--no-fund"], self.agent),
                (["run", "build"], self.agent),
            ],
        )

    def test_build_fails_when_npm_fails(self):
        with mock.patch.object(ai_plugins.shutil, "which", return_value="/usr/bin/npm"):
            with self.assertRaisesRegex(ai_plugins.AiPluginError, "npm ci"):
                ai_plugins.build_agent(self.workspace, run=lambda args, cwd: subprocess.CompletedProcess(args, 1))

    def test_build_refuses_upstreams_prebuilt_bundle(self):
        self.built(marker=False)
        with mock.patch.object(ai_plugins.shutil, "which", return_value="/usr/bin/npm"):
            with self.assertRaisesRegex(ai_plugins.AiPluginError, "Market Canopy provider"):
                ai_plugins.build_agent(self.workspace, run=lambda args, cwd: subprocess.CompletedProcess(args, 0))

    def test_build_needs_npm(self):
        with mock.patch.object(ai_plugins.shutil, "which", return_value=None):
            with self.assertRaisesRegex(ai_plugins.AiPluginError, "npm is not on PATH"):
                ai_plugins.build_agent(self.workspace)

    def test_deploy_copies_the_agent_into_every_platform(self):
        self.built()
        first = self.deployed("linux_64")
        second = self.deployed("linux_arm64")
        stale = os.path.join(first, ai_plugins.AGENT_GUID, "stale.js")
        write(stale, "old")
        self.assertEqual(ai_plugins.deploy(self.workspace, "marketcanopy"), [first, second])
        for plugins in (first, second):
            with open(os.path.join(plugins, ai_plugins.AGENT_GUID, "index.js"), encoding="utf-8") as handle:
                self.assertIn(ai_plugins.AGENT_MARKER, handle.read())
        self.assertFalse(os.path.exists(stale))

    def test_deploy_fails_with_nothing_deployed(self):
        self.built()
        with self.assertRaisesRegex(ai_plugins.AiPluginError, "no editors/sdkjs-plugins"):
            ai_plugins.deploy(self.workspace, "marketcanopy")

    def test_deploy_fails_without_the_in_editor_plugin(self):
        self.built()
        plugins = self.deployed("linux_64")
        os.remove(os.path.join(plugins, ai_plugins.EDITOR_AI_GUID, ai_plugins.EDITOR_PROVIDERS, "config.json"))
        with self.assertRaisesRegex(ai_plugins.AiPluginError, "not deployed"):
            ai_plugins.deploy(self.workspace, "marketcanopy")

    def test_deploy_refuses_other_providers_in_the_in_editor_plugin(self):
        self.built()
        self.deployed("linux_64", providers=("marketcanopy", "openai"))
        with self.assertRaisesRegex(ai_plugins.AiPluginError, "only marketcanopy"):
            ai_plugins.deploy(self.workspace, "marketcanopy")

    def test_deploy_needs_the_market_canopy_provider_file(self):
        self.built()
        self.deployed("linux_64", provider_file=False)
        with self.assertRaisesRegex(ai_plugins.AiPluginError, "no Market Canopy provider"):
            ai_plugins.deploy(self.workspace, "marketcanopy")


class BrandingDefaults(unittest.TestCase):
    def test_the_in_editor_ai_plugin_is_in_the_plugin_list(self):
        path = os.path.join(ROOT, "branding", "marketcanopy", "build_tools", "defaults")
        with open(path, encoding="utf-8") as handle:
            lines = [line.strip() for line in handle if line.startswith("sdkjs-plugin=")]
        self.assertEqual(len(lines), 1)
        plugins = lines[0].split("=", 1)[1].strip('"').split(", ")
        self.assertIn("ai", plugins)


class MacAddonMode(unittest.TestCase):
    def test_macos_build_does_not_turn_on_the_addon(self):
        commands = []
        ctx = package.Context("/work", "/out", "mac-arm64", "10.0.0-mc.1", "10.0.0", "1", True, False, True)
        with mock.patch.object(package.util, "run", side_effect=lambda args, **_: commands.append(args) or ""):
            package.mac_stage(ctx)
        xcodebuild = [args for args in commands if args[0] == "xcodebuild"]
        self.assertEqual(len(xcodebuild), 1)
        defines = [arg for arg in xcodebuild[0] if arg.startswith("GCC_PREPROCESSOR_DEFINITIONS=")]
        self.assertEqual(len(defines), 1)
        self.assertNotIn("USE_ADDON_MODE", defines[0])
        self.assertIn("OO_BRANDING=marketcanopy", defines[0])


if __name__ == "__main__":
    unittest.main()
