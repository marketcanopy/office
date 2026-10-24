import unittest

from officebuild import targets


class ParseTargets(unittest.TestCase):
    def test_groups_expand_in_order_without_duplicates(self):
        self.assertEqual(targets.parse_targets("linux,linux-x64"), ["linux-x64", "linux-arm64"])
        self.assertEqual(len(targets.parse_targets("all")), 6)

    def test_unknown_target_is_refused(self):
        with self.assertRaises(targets.TargetError):
            targets.parse_targets("win-x86")

    def test_empty_is_refused(self):
        with self.assertRaises(targets.TargetError):
            targets.parse_targets(" , ")


class Plan(unittest.TestCase):
    def plan(self, os_name, arch, wanted="all"):
        return targets.plan(targets.Host(os_name, arch), targets.parse_targets(wanted))

    def test_each_host_builds_its_own_targets_and_lists_the_rest(self):
        build, skipped = self.plan("windows", "x64")
        self.assertEqual(build, ["win-x64", "win-arm64"])
        self.assertEqual([name for name, _ in skipped], ["mac-arm64", "mac-x64", "linux-x64", "linux-arm64"])
        self.assertEqual(self.plan("linux", "x64")[0], ["linux-x64", "linux-arm64"])
        self.assertEqual(self.plan("macos", "arm64")[0], ["mac-arm64", "mac-x64"])

    def test_skipped_targets_carry_a_reason(self):
        _, skipped = self.plan("linux", "x64", "win-x64")
        self.assertIn("windows host", skipped[0][1])

    def test_arm64_linux_host_does_not_build_x64(self):
        build, skipped = self.plan("linux", "arm64", "linux")
        self.assertEqual(build, ["linux-arm64"])
        self.assertEqual(skipped[0][0], "linux-x64")

    def test_windows_targets_need_an_x64_host(self):
        build, _ = self.plan("windows", "arm64", "windows")
        self.assertEqual(build, [])


if __name__ == "__main__":
    unittest.main()
