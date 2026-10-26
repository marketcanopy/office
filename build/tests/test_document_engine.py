import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "branding", "marketcanopy", "build_tools"))

import document_engine  # noqa: E402

RUNPATH = " 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN:$ORIGIN/system]\n"


def write(path, text="x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


class DocumentEngine(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.workspace = self.tmp.name
        self.calls = []

    def tearDown(self):
        self.tmp.cleanup()

    def deployed(self, platform, product="desktopeditors", executable="x2t"):
        converter = os.path.join(self.workspace, "build_tools", "out", platform, "marketcanopy", product, "converter")
        write(os.path.join(converter, executable))
        return converter

    def built(self, platform, executable="docbuilder", branded=True):
        parts = ["core", "build"] + (["marketcanopy"] if branded else []) + ["bin", platform, executable]
        write(os.path.join(self.workspace, *parts), "docbuilder " + platform)

    def run_tool(self, stdout="", returncode=0):
        def run(args, **_):
            self.calls.append(args)
            return subprocess.CompletedProcess(args, returncode, stdout, "")

        return run

    def deploy(self, run, host="linux"):
        with mock.patch.object(document_engine.shutil, "which", return_value="/usr/bin/readelf"):
            return document_engine.deploy(self.workspace, "marketcanopy", run=run, host=host)

    def test_copies_docbuilder_beside_x2t_and_checks_its_origin_rpath(self):
        converter = self.deployed("linux_64")
        self.built("linux_64")
        self.assertEqual(self.deploy(self.run_tool(RUNPATH)), [os.path.join(converter, "docbuilder")])
        with open(os.path.join(converter, "docbuilder"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "docbuilder linux_64")
        self.assertEqual(self.calls, [["/usr/bin/readelf", "-d", os.path.join(converter, "docbuilder")]])

    def test_refuses_a_linux_docbuilder_that_would_not_find_its_libraries(self):
        self.deployed("linux_64")
        self.built("linux_64")
        with self.assertRaisesRegex(document_engine.DocumentEngineError, r"no \$ORIGIN"):
            self.deploy(self.run_tool(" 0x1 (NEEDED) Shared library: [libdoctrenderer.so]\n"))

    def test_windows_takes_the_exe_and_an_unbranded_core_build(self):
        converter = self.deployed("win_64", product="DesktopEditors", executable="x2t.exe")
        self.built("win_64", executable="docbuilder.exe", branded=False)
        self.assertEqual(self.deploy(self.run_tool(), host="win32"), [os.path.join(converter, "docbuilder.exe")])
        self.assertEqual(self.calls, [])

    def test_macos_points_docbuilder_at_its_libraries_through_rpath(self):
        converter = self.deployed("mac_arm64")
        self.built("mac_arm64")
        self.deploy(self.run_tool(), host="darwin")
        target = os.path.join(converter, "docbuilder")
        self.assertIn(
            ["install_name_tool", "-change", "libdoctrenderer.dylib", "@rpath/libdoctrenderer.dylib", target],
            self.calls,
        )
        self.assertEqual(self.calls[-1], ["install_name_tool", "-add_rpath", "@executable_path", target])

    def test_fails_when_the_core_build_has_no_docbuilder_or_nothing_was_deployed(self):
        self.deployed("linux_64")
        with self.assertRaisesRegex(
            document_engine.DocumentEngineError, "core/build/marketcanopy/bin/linux_64/docbuilder is missing"
        ):
            self.deploy(self.run_tool(RUNPATH))
        self.tmp.cleanup()
        self.tmp = tempfile.TemporaryDirectory()
        self.workspace = self.tmp.name
        with self.assertRaisesRegex(document_engine.DocumentEngineError, "no deployed converter"):
            self.deploy(self.run_tool(RUNPATH))


if __name__ == "__main__":
    unittest.main()
