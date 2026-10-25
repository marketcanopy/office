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
        self.install_sdk("10.0.20348.0")
        folder, env = toolchain.pin_windows_sdk(self.work, "/vs/VC/Auxiliary/Build", "10.0.20348.0", False)
        with open(os.path.join(folder, "vcvarsall.bat"), newline="") as handle:
            self.assertEqual(handle.read(), '@call "/vs/VC/Auxiliary/Build/vcvarsall.bat" %* 10.0.20348.0\r\n')
        with open(env["BOOST_BUILD_USER_CONFIG"]) as handle:
            config = handle.read()
        self.assertEqual(config, 'using msvc : 14.2 : : <setup>"%s" ;\n' % os.path.join(folder, "vcvarsall.bat"))

    def test_backslashes_are_escaped_for_jam(self):
        self.install_sdk("10.0.20348.0")
        work = os.path.join(self.work, "a\\b")
        _, env = toolchain.pin_windows_sdk(work, "/vs", "10.0.20348.0", False)
        with open(env["BOOST_BUILD_USER_CONFIG"]) as handle:
            self.assertIn("a\\\\b", handle.read())

    def test_missing_sdk_is_refused_with_the_component_to_add(self):
        with self.assertRaises(util.BuildError) as raised:
            toolchain.pin_windows_sdk(self.work, "/vs", "10.0.20348.0", False)
        self.assertIn("Windows10SDK.20348", str(raised.exception))

    def test_dry_run_writes_nothing(self):
        folder, _ = toolchain.pin_windows_sdk(self.work, "/vs", "10.0.20348.0", True)
        self.assertFalse(os.path.exists(folder))


if __name__ == "__main__":
    unittest.main()
