import hashlib
import io
import os
import tarfile
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


class PinIcuSdk(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.rsp = os.path.join(self.work, "core", "Common", "3dParty", "icu", "Directory.Build.rsp")

    def test_arm64_build_sets_the_sdk_above_icus_solution(self):
        self.assertEqual(toolchain.pin_icu_sdk(self.work, "10.0.22621.0", False), self.rsp)
        with open(self.rsp) as handle:
            self.assertEqual(handle.read(), "/p:WindowsTargetPlatformVersion=10.0.22621.0\n")

    def test_build_without_arm64_removes_a_left_over_file(self):
        toolchain.pin_icu_sdk(self.work, "10.0.22621.0", False)
        toolchain.pin_icu_sdk(self.work, "", False)
        self.assertFalse(os.path.exists(self.rsp))
        toolchain.pin_icu_sdk(self.work, "", False)

    def test_dry_run_writes_nothing(self):
        toolchain.pin_icu_sdk(self.work, "10.0.22621.0", True)
        self.assertFalse(os.path.exists(self.rsp))


class DepotTools(unittest.TestCase):
    def test_path_is_absolute_and_where_v8_89_clones_it(self):
        path = toolchain.depot_tools("work")
        self.assertTrue(os.path.isabs(path))
        expected = os.path.join(os.path.abspath("work"), "core", "Common", "3dParty", "v8_89", "depot_tools")
        self.assertEqual(path, expected)


class LinuxToolchain(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.work, "build_tools", "tools", "linux"))
        self.cmake_bin = os.path.join(self.work, toolchain.LINUX_CMAKE, "bin")
        os.makedirs(self.cmake_bin)
        open(os.path.join(self.cmake_bin, "cmake"), "w").close()
        patcher = mock.patch.object(util, "run")
        self.run = patcher.start()
        self.addCleanup(patcher.stop)

    def commands(self):
        return [call.args[0] for call in self.run.call_args_list]

    def test_base_packages_come_before_python_sh(self):
        toolchain.linux(self.work, ["linux-x64"], True, False)
        commands = self.commands()
        base = ["sudo", "apt-get", "install", "-y", "--no-install-recommends", *toolchain.LINUX_BASE_PACKAGES]
        install = commands.index(base)
        self.assertLess(commands.index(["sudo", "apt-get", "update"]), install)
        self.assertLess(install, commands.index(["bash", "./python.sh"]))
        self.assertIn("wget", toolchain.LINUX_BASE_PACKAGES)

    def test_kitware_cmake_comes_first_on_path_and_cmake_sh_is_not_run(self):
        result = toolchain.linux(self.work, ["linux-x64"], True, False)
        self.assertEqual(result.env["PATH"].split(os.pathsep)[0], self.cmake_bin)
        self.assertNotIn(["sudo", "./cmake.sh"], self.commands())

    def test_packaging_tools_are_installed(self):
        # desktop-apps/package: debhelper for the .deb, the aarch64 strip for both arm64 packages.
        toolchain.linux(self.work, ["linux-arm64"], True, False)
        packaging = [c for c in self.commands() if "fakeroot" in c]
        self.assertEqual(len(packaging), 1)
        for package in ("m4", "rpm", "debhelper", "binutils-aarch64-linux-gnu"):
            self.assertIn(package, packaging[0])

    def test_without_install_deps_nothing_is_installed(self):
        toolchain.linux(self.work, ["linux-x64"], False, False)
        self.assertFalse([c for c in self.commands() if c[:1] == ["sudo"]])


class LinuxCmake(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp()
        payload = io.BytesIO()
        with tarfile.open(fileobj=payload, mode="w:gz") as tar:
            data = b"#!/bin/sh\n"
            info = tarfile.TarInfo(toolchain.LINUX_CMAKE + "/bin/cmake")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        self.archive = payload.getvalue()
        patcher = mock.patch.object(
            toolchain.urllib.request, "urlopen", side_effect=lambda url: io.BytesIO(self.archive)
        )
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_verified_archive_is_unpacked_in_the_work_directory(self):
        digest = hashlib.sha256(self.archive).hexdigest()
        with mock.patch.object(toolchain, "LINUX_CMAKE_SHA256", digest):
            bin_dir = toolchain._linux_cmake(self.work, False)
        self.assertEqual(bin_dir, os.path.join(self.work, toolchain.LINUX_CMAKE, "bin"))
        self.assertTrue(os.path.isfile(os.path.join(bin_dir, "cmake")))
        self.assertFalse(os.path.exists(os.path.join(self.work, toolchain.LINUX_CMAKE + ".tar.gz")))

    def test_wrong_digest_is_refused_and_nothing_is_unpacked(self):
        with self.assertRaises(util.BuildError):
            toolchain._linux_cmake(self.work, False)
        self.assertEqual(os.listdir(self.work), [])


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


class WindowsBuildTools(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(util, "run")
        self.run = patcher.start()
        self.addCleanup(patcher.stop)

    def commands(self):
        return [call.args[0] for call in self.run.call_args_list]

    def test_perl_py_and_7zip_are_installed_when_missing(self):
        with mock.patch.object(toolchain.shutil, "which", return_value=None):
            with self.assertRaises(util.BuildError) as raised:
                toolchain.windows_build_tools(True, False)
        choco = [c[2] for c in self.commands() if c[:2] == ["choco", "install"]]
        self.assertEqual(choco, ["strawberryperl", "python3", "7zip"])
        self.assertIn("perl Configure", str(raised.exception))
        self.assertIn("py -3", str(raised.exception))

    def test_strawberry_perl_outside_path_is_put_first(self):
        def which(name):
            return {"py": "C:\\Windows\\py.exe", "7z": "C:\\Program Files\\7-Zip\\7z.exe"}.get(name)

        perl_exe = os.path.join(toolchain.STRAWBERRY_PERL_BIN, "perl.exe")
        with mock.patch.object(toolchain.shutil, "which", side_effect=which), mock.patch.object(
            toolchain.os.path, "isfile", side_effect=lambda path: path == perl_exe
        ):
            self.assertEqual(toolchain.windows_build_tools(True, False), [toolchain.STRAWBERRY_PERL_BIN])

    def test_git_bash_msys_perl_is_put_behind_strawberry_perl(self):
        # Under `shell: bash` the first perl on PATH is Git's MSYS build, for
        # which OpenSSL's Configure writes no makefile.
        perl_exe = os.path.join(toolchain.STRAWBERRY_PERL_BIN, "perl.exe")
        with mock.patch.object(
            toolchain.shutil, "which", return_value=r"C:\Program Files\Git\usr\bin\perl.exe"
        ), mock.patch.object(toolchain, "_tool_output", return_value="msys"), mock.patch.object(
            toolchain.os.path, "isfile", side_effect=lambda path: path == perl_exe
        ):
            self.assertEqual(toolchain.windows_build_tools(True, False), [toolchain.STRAWBERRY_PERL_BIN])
        self.assertEqual(self.commands(), [])

    def test_git_bash_msys_perl_alone_installs_strawberry_perl(self):
        with mock.patch.object(toolchain.shutil, "which", return_value="found"), mock.patch.object(
            toolchain, "_tool_output", return_value="msys"
        ), mock.patch.object(toolchain.os.path, "isfile", return_value=False):
            with self.assertRaises(util.BuildError) as raised:
                toolchain.windows_build_tools(True, False)
        choco = [c[2] for c in self.commands() if c[:2] == ["choco", "install"]]
        self.assertEqual(choco, ["strawberryperl"])
        self.assertIn("Windows Perl", str(raised.exception))

    def test_7zip_outside_path_is_put_first(self):
        # desktop-apps' make_zip.ps1 runs `7z`; Chocolatey's 7zip is not on PATH
        # for the process that installed it.
        seven_zip = os.path.join(toolchain.SEVEN_ZIP_DIR, "7z.exe")
        with mock.patch.object(
            toolchain.shutil, "which", side_effect=lambda name: None if name == "7z" else "found"
        ), mock.patch.object(toolchain, "_tool_output", return_value="MSWin32"), mock.patch.object(
            toolchain.os.path, "isfile", side_effect=lambda path: path == seven_zip
        ):
            self.assertEqual(toolchain.windows_build_tools(True, False), [toolchain.SEVEN_ZIP_DIR])
        self.assertEqual(self.commands(), [])

    def test_missing_7zip_is_installed_then_refused(self):
        with mock.patch.object(
            toolchain.shutil, "which", side_effect=lambda name: None if name == "7z" else "found"
        ), mock.patch.object(toolchain, "_tool_output", return_value="MSWin32"), mock.patch.object(
            toolchain.os.path, "isfile", return_value=False
        ):
            with self.assertRaises(util.BuildError) as raised:
                toolchain.windows_build_tools(True, False)
        choco = [c[2] for c in self.commands() if c[:2] == ["choco", "install"]]
        self.assertEqual(choco, ["7zip"])
        self.assertIn("7z", str(raised.exception))

    def test_present_tools_install_nothing(self):
        with mock.patch.object(toolchain.shutil, "which", return_value="found"), mock.patch.object(
            toolchain, "_tool_output", return_value="MSWin32"
        ):
            self.assertEqual(toolchain.windows_build_tools(True, False), [])
        self.assertEqual(self.commands(), [])

    def test_without_install_deps_a_missing_tool_is_refused(self):
        with mock.patch.object(toolchain.shutil, "which", return_value=None):
            with self.assertRaises(util.BuildError):
                toolchain.windows_build_tools(False, False)
        self.assertEqual(self.commands(), [])
