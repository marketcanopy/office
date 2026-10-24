# Market Canopy Office: Linux packaging names.
# Copyright (C) 2026 Market Canopy
#
# Read by desktop-apps package/Makefile (ONLYOFFICE, Copyright (C) Ascensio
# System SIA) through `-include $(BRANDING_DIR)/branding.mk`; build_tools passes
# BRANDING_DIR=../../marketcanopy/desktop-apps/package. The Makefile computes
# PACKAGE_NAME, DESKTOPEDITORS_PREFIX, DESKTOPEDITORS_EXEC and HELP_GLOB with
# `:=` before it includes this file, so they are computed again here.
#
# Licensed under the GNU Affero General Public License version 3 with the
# additional terms in the LICENSE file at the root of this repository.
# SPDX-License-Identifier: AGPL-3.0-only

COMPANY_NAME = Market Canopy
PRODUCT_NAME = Office
PRODUCT_NAME_SHORT = Office
COMPANY_NAME_LOW = marketcanopy
PRODUCT_NAME_LOW = office

PUBLISHER_NAME = Market Canopy
PUBLISHER_URL = https://marketcanopy.com
SUPPORT_URL = https://marketcanopy.com/help
# Debian's Maintainer field needs an address and Market Canopy has not
# published a support mailbox for Office yet (owner item in evidence/P03.md).
# The reserved .invalid domain keeps the placeholder from reaching anyone.
SUPPORT_MAIL = office-support@marketcanopy.invalid
SCHEME_HANDLER = mc-office

PACKAGE_NAME := marketcanopy-office
PACKAGE_OPENSOURCE := $(PACKAGE_NAME)
# There is no commercial edition; the name only keeps the templates valid.
PACKAGE_COMMERCIAL := $(PACKAGE_NAME)-commercial
ifeq ($(PACKAGE_EDITION), commercial)
$(error Market Canopy Office has no commercial edition; use PACKAGE_EDITION=opensource)
endif

DESKTOPEDITORS_PREFIX := marketcanopy/office
DESKTOPEDITORS_EXEC := marketcanopy-office
HELP_GLOB := opt/$(DESKTOPEDITORS_PREFIX)/editors/web-apps/apps/*/main/resources/help
