import os
import tempfile
import unittest
from unittest import mock

from officebuild import toolchain, util


class PinWindowsSdk(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.kits = tempfile.mkdtemp()
        self.environ = mock.patch.dict(os.environ, {"ProgramFiles(x86)": self.kits})
        self.environ.start()
        self.addCleanup(self.environ.stop)

    def install_sdk(self, version):
        um = os.path.join(self.kits, "Windows Kits", "10", "Include", version, "um")
        os.makedirs(um)
        open(os.path.join(um, "winsdkver.h"), "w").close()

    def test_wrapper_passes_the_version_after_the_callers_arguments(self):
        self.install_sdk("10.0.22621.0")
        folder, env = toolchain.pin_windows_sdk(self.work, "/vs/VC/Auxiliary/Build", "10.0.22621.0", False)
        with open(os.path.join(folder, "vcvarsall.bat"), newline="") as handle:
            self.assertEqual(handle.read(), '@call "/vs/VC/Auxiliary/Build/vcvarsall.bat" %* 10.0.22621.0\r\n')
        with open(env["BOOST_BUILD_USER_CONFIG"]) as handle:
            config = handle.read()
        self.assertEqual(config, 'using msvc : 14.2 : : <setup>"%s" ;\n' % os.path.join(folder, "vcvarsall.bat"))

    def test_backslashes_are_escaped_for_jam(self):
        self.install_sdk("10.0.22621.0")
        work = os.path.join(self.work, "a\\b")
        _, env = toolchain.pin_windows_sdk(work, "/vs", "10.0.22621.0", False)
        with open(env["BOOST_BUILD_USER_CONFIG"]) as handle:
            self.assertIn("a\\\\b", handle.read())

    def test_missing_sdk_is_refused_with_the_component_to_add(self):
        with self.assertRaises(util.BuildError) as raised:
            toolchain.pin_windows_sdk(self.work, "/vs", "10.0.22621.0", False)
        self.assertIn("10.0.22621.0", str(raised.exception))

    def test_dry_run_writes_nothing(self):
        folder, _ = toolchain.pin_windows_sdk(self.work, "/vs", "10.0.22621.0", True)
        self.assertFalse(os.path.exists(folder))


class DepotTools(unittest.TestCase):
    def test_path_is_absolute_and_where_v8_89_clones_it(self):
        path = toolchain.depot_tools("work")
        self.assertTrue(os.path.isabs(path))
        expected = os.path.join(os.path.abspath("work"), "core", "Common", "3dParty", "v8_89", "depot_tools")
        self.assertEqual(path, expected)


class MajorVersion(unittest.TestCase):
    def test_node_and_java_outputs(self):
        self.assertEqual(toolchain.major_version("v22.23.3\n"), 22)
        self.assertEqual(toolchain.major_version('openjdk version "17.0.20" 2026-07-21\nOpenJDK Runtime'), 17)
        self.assertEqual(toolchain.major_version('java version "1.8.0_462"\n'), 8)

    def test_missing_tool_is_zero(self):
        self.assertEqual(toolchain.major_version(""), 0)


class NodeVersion(unittest.TestCase):
    def test_major_and_minor(self):
        self.assertEqual(toolchain.node_version("v22.23.3\n"), (22, 23))
        self.assertEqual(toolchain.node_version(""), (0, 0))


class CheckRequirements(unittest.TestCase):
    def outputs(self, node, java):
        return lambda args, env: {"node": node, "java": java}.get(args[0], "tool 1.0")

    def test_old_node_and_missing_java_are_refused(self):
        with mock.patch.object(toolchain, "_tool_output", self.outputs("v18.20.0", "")):
            with self.assertRaises(util.BuildError) as raised:
                toolchain.check_requirements({}, False)
        self.assertIn("Node.js 22.12", str(raised.exception))
        self.assertIn("Java 11", str(raised.exception))

    def test_node_20_and_early_22_are_refused(self):
        for node in ("v20.19.5", "v22.11.0"):
            with mock.patch.object(toolchain, "_tool_output", self.outputs(node, 'openjdk version "17.0.20"')):
                with self.assertRaises(util.BuildError):
                    toolchain.check_requirements({}, False)

    def test_newer_node_passes(self):
        for node in ("v22.12.0", "v24.1.0"):
            with mock.patch.object(toolchain, "_tool_output", self.outputs(node, 'openjdk version "17.0.20"')):
                toolchain.check_requirements({}, False)

    def test_hosted_versions_pass(self):
        with mock.patch.object(toolchain, "_tool_output", self.outputs("v22.23.3", 'openjdk version "17.0.20"')):
            toolchain.check_requirements({}, False)


if __name__ == "__main__":
    unittest.main()
