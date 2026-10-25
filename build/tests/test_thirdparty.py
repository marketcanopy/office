import os
import tempfile
import unittest

from officebuild import thirdparty, util


class ThirdParty(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.third_party = os.path.join(self.work, "core", "Common", "3dParty")

    def touch(self, *parts):
        path = os.path.join(self.third_party, *parts)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w").close()

    def build_all(self, platform):
        self.touch("openssl", "build", platform, "include", "openssl", "sha.h")
        self.touch("openssl", "build", platform, "lib", "libcrypto.lib")
        self.touch("openssl", "build", platform, "lib", "libssl.lib")
        for name in ("icuuc.lib", "icudt.lib", "icuuc74.dll", "icudt74.dll"):
            self.touch("icu", platform, "build", name)
        self.touch("icu", "icu", "include", "unicode", "utypes.h")

    def test_empty_openssl_folder_left_by_a_failed_build_is_removed(self):
        # build_tools creates the folder before `perl Configure`, then skips it.
        self.build_all("win_64")
        empty = os.path.join(self.third_party, "openssl", "build", "win_arm64")
        os.makedirs(empty)
        self.build_all("win_arm64")
        for name in os.listdir(os.path.join(empty, "lib")):
            os.remove(os.path.join(empty, "lib", name))
        dropped = thirdparty.drop_incomplete(self.work, ["win_arm64"])
        self.assertEqual(dropped, [empty])
        self.assertFalse(os.path.exists(empty))
        self.assertTrue(os.path.isdir(os.path.join(self.third_party, "openssl", "build", "win_64")))

    def test_missing_icu_headers_rebuild_every_icu_platform(self):
        self.build_all("win_64")
        self.build_all("win_arm64")
        os.remove(os.path.join(self.third_party, "icu", "icu", "include", "unicode", "utypes.h"))
        dropped = thirdparty.drop_incomplete(self.work, ["win_arm64"])
        self.assertEqual(
            sorted(dropped),
            sorted(os.path.join(self.third_party, "icu", p, "build") for p in ("win_64", "win_arm64")),
        )

    def test_win_arm64_also_checks_the_win_64_build_build_tools_adds(self):
        self.build_all("win_arm64")
        absent = thirdparty.missing(self.work, ["win_arm64"])
        self.assertIn(os.path.join(self.third_party, "openssl", "build", "win_64", "lib", "libssl.lib"), absent)

    def test_complete_outputs_are_kept_and_pass(self):
        self.build_all("win_64")
        self.assertEqual(thirdparty.drop_incomplete(self.work, ["win_64"]), [])
        thirdparty.check(self.work, ["win_64"])

    def test_check_names_each_missing_file(self):
        with self.assertRaises(util.BuildError) as raised:
            thirdparty.check(self.work, ["win_64"])
        self.assertIn("sha.h", str(raised.exception))
        self.assertIn("icudt74.dll", str(raised.exception))

    def test_dry_run_removes_nothing(self):
        folder = os.path.join(self.third_party, "openssl", "build", "win_64")
        os.makedirs(folder)
        self.assertEqual(thirdparty.drop_incomplete(self.work, ["win_64"], dry_run=True), [folder])
        self.assertTrue(os.path.isdir(folder))

    def test_other_hosts_have_nothing_to_check(self):
        self.assertEqual(thirdparty.products(self.work, ["linux_64", "mac_arm64"]), {})
        thirdparty.check(self.work, ["linux_64"])
