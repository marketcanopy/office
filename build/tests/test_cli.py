import unittest

from officebuild import cli


class ThirdPartyOnly(unittest.TestCase):
    def test_stops_after_compile(self):
        args = cli.parse_args(["--third-party-only", "--package"])
        self.assertEqual(cli.phases_for(args), ["prepare", "compile"])

    def test_driver_is_valid_python_that_ends_in_make_common(self):
        compile(cli.THIRD_PARTY_ONLY, "third-party-only", "exec")
        self.assertEqual(cli.THIRD_PARTY_ONLY.strip().splitlines()[-1], "make_common.make()")


if __name__ == "__main__":
    unittest.main()
