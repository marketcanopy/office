import os
import stat
import tempfile
import unittest
from unittest import mock

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
        self.touch("harfbuzz", "harfbuzz", "src", "hb.h")
        self.touch("harfbuzz", "harfbuzz.pri")

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

    def test_harfbuzz_clone_without_its_qmake_include_is_removed(self):
        # A run killed after the clone left harfbuzz/harfbuzz, so make.py never wrote harfbuzz.pri.
        self.build_all("win_64")
        os.remove(os.path.join(self.third_party, "harfbuzz", "harfbuzz.pri"))
        clone = os.path.join(self.third_party, "harfbuzz", "harfbuzz")
        self.assertEqual(thirdparty.drop_incomplete(self.work, ["win_64"]), [clone])
        self.assertFalse(os.path.exists(clone))

    def test_read_only_git_objects_are_made_writable_and_removed(self):
        # Windows refuses to delete a read-only file, which git leaves its pack files as.
        self.build_all("win_64")
        os.remove(os.path.join(self.third_party, "harfbuzz", "harfbuzz.pri"))
        pack = os.path.join(self.third_party, "harfbuzz", "harfbuzz", ".git", "objects", "pack", "pack-1.idx")
        os.makedirs(os.path.dirname(pack))
        open(pack, "w").close()
        os.chmod(pack, stat.S_IREAD)
        real_remove = os.remove
        refused = []

        def windows_remove(path, *args, **kwargs):
            if not os.stat(path, dir_fd=kwargs.get("dir_fd")).st_mode & stat.S_IWRITE:
                refused.append(os.path.basename(path))
                raise PermissionError(13, "Access is denied", path)
            real_remove(path, *args, **kwargs)

        with mock.patch("os.unlink", windows_remove):
            thirdparty.drop_incomplete(self.work, ["win_64"])
        self.assertIn("pack-1.idx", refused)
        self.assertFalse(os.path.exists(os.path.join(self.third_party, "harfbuzz", "harfbuzz")))

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
