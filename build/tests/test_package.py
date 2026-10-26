import os
import shutil
import tempfile
import unittest
from unittest import mock

from officebuild import package, util


class LinuxInstallers(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.package_dir = os.path.join(self.work.name, "desktop-apps", "package")
        patcher = mock.patch.object(util, "run")
        patcher.start()
        self.addCleanup(patcher.stop)

    def context(self):
        return package.Context(
            work_root=self.work.name,
            out_root=os.path.join(self.work.name, "out"),
            target="linux-arm64",
            version="10.0.0-mc.0",
            product_version="10.0.0",
            build_number="0",
            branded=True,
            sign=False,
            dry_run=False,
        )

    def touch(self, *parts):
        path = os.path.join(self.package_dir, *parts)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w").close()

    def test_the_branding_packages_tar_deb_and_rpm(self):
        # The branding's package_branding.py makes tar, deb and rpm, no SUSE rpm.
        self.touch("deb", "marketcanopy-office_10.0.0-0_arm64.deb")
        self.touch("rpm", "build", "RPMS", "aarch64", "marketcanopy-office-10.0.0-0.aarch64.rpm")
        self.touch("tar", "marketcanopy-office-10.0.0-0-aarch64.tar.xz")
        self.touch("tar", "marketcanopy-office-help-10.0.0-0-any.tar.xz")
        self.assertEqual(
            package.linux_installers(self.context()),
            [
                "MarketCanopyOffice-10.0.0-mc.0-linux-arm64-development.deb",
                "MarketCanopyOffice-10.0.0-mc.0-linux-arm64-development.rpm",
                "MarketCanopyOffice-10.0.0-mc.0-linux-arm64-development.tar.xz",
            ],
        )

    def test_a_missing_rpm_fails(self):
        self.touch("deb", "marketcanopy-office_10.0.0-0_arm64.deb")
        self.touch("tar", "marketcanopy-office-10.0.0-0-aarch64.tar.xz")
        with self.assertRaises(util.BuildError) as raised:
            package.linux_installers(self.context())
        self.assertIn("no .rpm", str(raised.exception))


UNOFFICIAL = "https://raw.githubusercontent.com/jrsoftware/issrc/is-6_7_1/Files/Languages/Unofficial/%s"
COMMON_ISS = """[Languages]
Name: en; MessagesFile: compiler:Default.isl;
Name: bg; MessagesFile: compiler:Languages\\Bulgarian.isl;
Name: el; MessagesFile: compiler:Languages\\Greek.isl;
;Name: et; MessagesFile: compiler:Languages\\Estonian.isl;
#if Int(DecodeVer(PREPROCVER,1)) < 6
Name: vi; MessagesFile: compiler:Languages\\Vietnamese.islu;
#else
Name: vi; MessagesFile: compiler:Languages\\Vietnamese.isl;
#endif
Name: si; MessagesFile: compiler:Languages\\Sinhala.islu;
"""


class WindowsInstallers(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.inno = os.path.join(self.work.name, "Inno Setup 6")
        os.makedirs(os.path.join(self.inno, "Languages"))
        open(os.path.join(self.inno, "ISCC.exe"), "w").close()
        open(os.path.join(self.inno, "Languages", "Bulgarian.isl"), "w").close()
        self.inno_dir = os.path.join(self.work.name, "desktop-apps", "package", "inno")
        os.makedirs(self.inno_dir)
        with open(os.path.join(self.inno_dir, "common.iss"), "w") as handle:
            handle.write(COMMON_ISS)
        os.makedirs(os.path.join(self.work.name, "desktop-apps", "package", "build", "x64", "desktop"))
        self.runs = []
        patcher = mock.patch.object(util, "run", side_effect=lambda args, **kw: self.runs.append((args, kw)))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.fetched = []

        def fetch(url, path):
            self.fetched.append(url)
            open(path, "w").close()

        patcher = mock.patch.object(package, "_download", side_effect=fetch, create=True)
        patcher.start()
        self.addCleanup(patcher.stop)
        patcher = mock.patch.dict(os.environ, {"INNOPATH": self.inno})
        patcher.start()
        self.addCleanup(patcher.stop)

    def context(self):
        return package.Context(
            work_root=self.work.name,
            out_root=os.path.join(self.work.name, "out"),
            target="win-x64",
            version="10.0.0-mc.0",
            product_version="10.0.0",
            build_number="0",
            branded=True,
            sign=False,
            dry_run=False,
        )

    def test_inno_gets_the_unofficial_languages_common_iss_names(self):
        # common.iss names Greek and Vietnamese, which Inno Setup ships only
        # in its source tree's Unofficial folder; the compiler stops at Greek.
        with self.assertRaises(util.BuildError):  # no installers collected
            package.windows_installers(self.context())
        inno_args, inno_kw = [run for run in self.runs if "make_inno.ps1" in run[0]][0]
        innopath = inno_kw["env"]["INNOPATH"]
        self.assertNotEqual(innopath, self.inno)
        self.assertEqual(
            sorted(os.listdir(os.path.join(innopath, "Languages"))),
            ["Bulgarian.isl", "Greek.isl", "Sinhala.islu", "Vietnamese.isl"],
        )
        self.assertTrue(os.path.isfile(os.path.join(innopath, "ISCC.exe")))
        self.assertEqual(
            self.fetched,
            [UNOFFICIAL % "Greek.isl", UNOFFICIAL % "Sinhala.islu", UNOFFICIAL % "Vietnamese.isl"],
        )
        self.assertEqual(os.listdir(os.path.join(self.inno, "Languages")), ["Bulgarian.isl"])

    def test_the_chocolatey_shim_on_path_is_not_taken_for_inno_setup(self):
        # Chocolatey puts an iscc.exe shim in its bin folder, which has no
        # Languages folder; run 40299651728 copied that and failed on Albanian.
        shim = os.path.join(self.work.name, "chocolatey", "bin")
        os.makedirs(shim)
        os.environ.pop("INNOPATH")
        with mock.patch.object(shutil, "which", return_value=os.path.join(shim, "iscc.exe")):
            with mock.patch.object(package, "INNO_SETUP_DIR", self.inno):
                innopath = package.inno_setup(self.context())
        self.assertTrue(os.path.isfile(os.path.join(innopath, "ISCC.exe")))
        self.assertTrue(os.path.isfile(os.path.join(innopath, "Languages", "Greek.isl")))


class WindowsStageThemes(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        app = os.path.join(self.work.name, "build_tools", "out", "win_arm64", "marketcanopy", "Office")
        os.makedirs(os.path.join(app, "editors"))
        self.sdkjs = os.path.join(
            self.work.name, "desktop-apps", "package", "build", "arm64", "desktop", "editors", "sdkjs"
        )
        self.write(os.path.join(self.sdkjs, "slide", "themes", "themes.js"), "arm64")
        self.x64 = os.path.join(self.work.name, "x64-sdkjs")
        self.write(os.path.join(self.x64, "slide", "themes", "themes.js"), "x64")
        self.write(os.path.join(self.x64, "slide", "themes", "theme1", "thumbnail.png"), "x64")
        self.write(os.path.join(self.x64, "common", "Images", "themes_thumbnail.png"), "x64")
        self.write(os.path.join(self.x64, "word", "sdk-all.cache"), "x64 snapshot")
        patcher = mock.patch.object(util, "run")
        patcher.start()
        self.addCleanup(patcher.stop)

    def write(self, path, text):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as handle:
            handle.write(text)

    def read(self, *parts):
        with open(os.path.join(self.sdkjs, *parts)) as handle:
            return handle.read()

    def test_arm64_takes_x64s_theme_previews_but_not_its_snapshots(self):
        # An x64 host cannot run arm64's allthemesgen or x2t, so the staged
        # arm64 app gets x64's generated editor files; V8 snapshots are per CPU.
        ctx = package.Context(
            work_root=self.work.name,
            out_root=os.path.join(self.work.name, "out"),
            target="win-arm64",
            version="10.0.0-mc.0",
            product_version="10.0.0",
            build_number="0",
            branded=True,
            sign=False,
            dry_run=False,
            themes_from=self.x64,
        )
        package.windows_stage(ctx)
        self.assertEqual(self.read("slide", "themes", "themes.js"), "x64")
        self.assertEqual(self.read("slide", "themes", "theme1", "thumbnail.png"), "x64")
        self.assertEqual(self.read("common", "Images", "themes_thumbnail.png"), "x64")
        self.assertFalse(os.path.exists(os.path.join(self.sdkjs, "word", "sdk-all.cache")))


if __name__ == "__main__":
    unittest.main()
