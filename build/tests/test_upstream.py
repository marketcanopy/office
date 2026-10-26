import unittest

from officebuild import upstream

SHA = "76af764fcb777291098c73728044249b6893af6a"


class Parse(unittest.TestCase):
    def test_reads_tag_version_and_commit_pins(self):
        pins = upstream.parse(
            "# pins\ntag=v10.0.0.176\nversion=10.0.0\ncommit.core=%s\ncommit.onlyoffice.github.io=%s\n" % (SHA, SHA)
        )
        self.assertEqual(pins.tag, "v10.0.0.176")
        self.assertEqual(pins.branch, "tags/v10.0.0.176")
        self.assertEqual(pins.commit("core"), SHA)
        self.assertEqual(pins.commit("onlyoffice.github.io"), SHA)
        self.assertIsNone(pins.commit("web-apps"))

    def test_version_defaults_from_the_tag(self):
        self.assertEqual(upstream.parse("tag=v9.4.0.117").version, "9.4.0")

    def test_version_must_match_the_tag(self):
        with self.assertRaises(upstream.UpstreamError):
            upstream.parse("tag=v10.0.0.176\nversion=10.0.1")

    def test_commit_must_be_a_full_sha(self):
        with self.assertRaises(upstream.UpstreamError):
            upstream.parse("tag=v10.0.0.176\ncommit.core=76af764")

    def test_tag_is_required(self):
        with self.assertRaises(upstream.UpstreamError):
            upstream.parse("version=10.0.0")

    def test_load_needs_the_upstream_file(self):
        with self.assertRaises(upstream.UpstreamError):
            upstream.load("/nonexistent")


if __name__ == "__main__":
    unittest.main()
