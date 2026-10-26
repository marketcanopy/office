import os
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


if __name__ == "__main__":
    unittest.main()
