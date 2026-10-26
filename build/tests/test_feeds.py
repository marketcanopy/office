import json
import os
import sys
import unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "branding", "marketcanopy", "build_tools"))

import make  # noqa: E402

with open(os.path.join(ROOT, "branding", "marketcanopy", "settings.json"), encoding="utf-8") as handle:
    SETTINGS = json.load(handle)

FEED_KEYS = ("windowsAppcast", "windowsAppcastDev", "macosArmFeed", "macosX64Feed")


class ProductionFeeds(unittest.TestCase):
    def feeds(self, channel):
        with mock.patch.dict(os.environ, {"OFFICE_CHANNEL": channel}):
            return make.channel_feeds(SETTINGS, "production")

    def test_each_channel_gets_its_own_feeds(self):
        beta = self.feeds("beta")
        stable = self.feeds("stable")
        self.assertEqual(beta["windowsAppcast"], "https://updates.marketcanopy.com/office/beta/windows/appcast.json")
        self.assertEqual(
            stable["windowsAppcast"], "https://updates.marketcanopy.com/office/stable/windows/appcast.json"
        )
        self.assertEqual(
            stable["macosArmFeed"], "https://updates.marketcanopy.com/office/stable/macos/arm64/appcast.xml"
        )
        self.assertEqual(stable["macosX64Feed"], "https://updates.marketcanopy.com/office/stable/macos/x64/appcast.xml")
        # The dev channel (--appcast-dev-channel) is beta for both.
        self.assertEqual(beta["windowsAppcastDev"], stable["windowsAppcastDev"])
        self.assertIn("/office/beta/", stable["windowsAppcastDev"])

    def test_every_feed_is_market_canopy_https(self):
        for key in FEED_KEYS:
            value = self.feeds("stable")[key]
            self.assertTrue(value.startswith("https://updates.marketcanopy.com/office/"), value)
            self.assertNotIn("{channel}", value)
            self.assertNotIn(value, SETTINGS["updates"]["upstreamDefaults"])

    def test_a_production_build_without_a_channel_fails(self):
        for channel in ("", "staging", "nightly"):
            with mock.patch.dict(os.environ, {"OFFICE_CHANNEL": channel}), self.assertRaises(SystemExit):
                make.channel_feeds(SETTINGS, "production")

    def test_a_staging_build_is_refused(self):
        with mock.patch.dict(os.environ, {"OFFICE_FLAVOR": "staging", "OFFICE_CHANNEL": "beta"}), self.assertRaises(
            SystemExit
        ):
            make.resolve_feeds(SETTINGS)

    def test_resolve_feeds_exports_the_channel_feeds(self):
        env = {"OFFICE_FLAVOR": "production", "OFFICE_CHANNEL": "beta"}
        for name in (
            "DESKTOP_URL_UPDATES_MAIN_CHANNEL",
            "DESKTOP_URL_UPDATES_DEV_CHANNEL",
            "OFFICE_SPARKLE_FEED_ARM",
            "OFFICE_SPARKLE_FEED_X86_64",
        ):
            env[name] = ""
        with mock.patch.dict(os.environ, env), mock.patch.object(make.sys, "platform", "linux"):
            self.assertEqual(make.resolve_feeds(SETTINGS), "production")
            self.assertEqual(
                os.environ["DESKTOP_URL_UPDATES_MAIN_CHANNEL"],
                "https://updates.marketcanopy.com/office/beta/windows/appcast.json",
            )
            self.assertEqual(
                os.environ["OFFICE_SPARKLE_FEED_ARM"],
                "https://updates.marketcanopy.com/office/beta/macos/arm64/appcast.xml",
            )


if __name__ == "__main__":
    unittest.main()
