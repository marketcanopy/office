#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Market Canopy Office: packaging names for build_tools' make_package.py.
# Copyright (C) 2026 Market Canopy LLC
#
# Modified version of build_tools/scripts/package_branding.py from ONLYOFFICE
# build_tools (Copyright (C) Ascensio System SIA), based on the tag recorded in
# UPSTREAM. Modified by Market Canopy on 2026-10-24: Market Canopy names, no
# ONLYOFFICE S3 bucket, Market Canopy update feeds, no Enterprise packages.
#
# Licensed under the GNU Affero General Public License version 3 with the
# additional terms in the LICENSE file at the root of this repository.
# SPDX-License-Identifier: AGPL-3.0-only
#
# make_package.py -R marketcanopy puts <workspace>/marketcanopy/build_tools/scripts
# on sys.path ahead of upstream's scripts, so this module replaces upstream's
# package_branding (ADR 0529 decision 1, overlay layer).

import json
import os

import package_utils as utils

_settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "settings.json")
with open(_settings_path, "r", encoding="utf-8") as _handle:
  _settings = json.load(_handle)
_identity = _settings["identity"]

# Not ONLYOFFICE: build_tools skips ONLYOFFICE-only steps (help removal, the
# Enterprise Sparkle feed) and passes BRANDING_DIR to the Linux makefile.
onlyoffice = False
company_name = _identity["companyName"]
# The deploy folder build_tools/out/<prefix>/<branding-name>.
company_name_l = _identity["brandingName"]
publisher_name = _identity["copyrightHolder"]
cert_name = _identity["copyrightHolder"]

# make_package.py uploads to S3 only with the "deploy" target, which office-build
# never passes; publishing is P12's office-release.yml. Empty values make an
# accidental upload fail instead of reaching ONLYOFFICE's bucket.
s3_bucket = ""
s3_region = ""
s3_base_url = ""

_flavor = os.environ.get("OFFICE_FLAVOR", "staging")
_feeds = _settings["updates"][_flavor]
# {channel} is OFFICE_CHANNEL, as in make.py's channel_feeds.
if any("{channel}" in str(_value) for _value in _feeds.values()):
  _channel = os.environ.get("OFFICE_CHANNEL", "")
  if _channel not in ("beta", "stable"):
    raise SystemExit("package_branding: the %s update feeds need OFFICE_CHANNEL beta or stable" % _flavor)
  _feeds = {_key: str(_value).replace("{channel}", _channel) for _key, _value in _feeds.items()}


if utils.is_windows():
  desktop_product_name = _identity["internalProductName"]
  desktop_product_name_s = desktop_product_name.replace(" ", "")
  desktop_package_name = _identity["internalCompanyName"] + "-" + desktop_product_name_s
  desktop_changes_dir = "desktop-apps/win-linux/package/windows/update/changes"

if utils.is_macos():
  # package_desktop.py builds the Xcode scheme (ONLYOFFICE-arm) and the
  # Info.plist path (Resources/ONLYOFFICE-arm) from this name, so it stays
  # upstream's: both are build-internal. The bundle (MarketCanopyOffice.app,
  # PRODUCT_NAME) and its identifier come from desktop-apps patch 0002. Do not
  # set ARCHIVES_DIR: that path writes ONLYOFFICE's help URL into Info.plist.
  desktop_package_name = "ONLYOFFICE"
  desktop_build_dir = "desktop-apps/macos"
  desktop_branding_dir = "desktop-apps/macos"
  desktop_updates_dir = "build/update"
  desktop_changes_dir = "ONLYOFFICE/update/updates/ONLYOFFICE/changes"
  sparkle_base_url = os.path.dirname(_feeds["macosArmFeed"]) if _feeds["macosArmFeed"] else ""
  sparkle_enterprise_base_url = ""
  desktop_appcast_main_url = _feeds["windowsAppcast"]
  desktop_appcast_dev_url = _feeds["windowsAppcastDev"]
  for _url in (desktop_appcast_main_url, desktop_appcast_dev_url):
    if not _url or "onlyoffice.com" in _url:
      raise SystemExit("package_branding: no Market Canopy update feed for the %s flavor" % _flavor)

builder_product_name = "Document Builder"

if utils.is_linux():
  desktop_make_targets = [
    {
      "make": "tar",
      "src": "tar/*.tar*",
      "dst": "desktop/linux/generic/"
    },
    {
      "make": "deb",
      "src": "deb/*.deb",
      "dst": "desktop/linux/debian/"
    },
    {
      "make": "rpm",
      "src": "rpm/build/RPMS/*/*.rpm",
      "dst": "desktop/linux/rhel/"
    }
  ]
  server_make_targets = []
