# Market Canopy Office: the account module (ADR 0524). Included once by
# ascdocumentscore.pro (patches/desktop-sdk/0002). office-build exports
# OFFICE_VERSION and OFFICE_FLAVOR for the build; a build without them is a
# staging development build.
#
# Copyright (C) 2026 Market Canopy LLC
# SPDX-License-Identifier: AGPL-3.0-only

MC_ACCOUNT_DIR = $$PWD

HEADERS += \
    $$MC_ACCOUNT_DIR/account.h \
    $$MC_ACCOUNT_DIR/additional.h \
    $$MC_ACCOUNT_DIR/contract.h \
    $$MC_ACCOUNT_DIR/crypto.h \
    $$MC_ACCOUNT_DIR/hooks.h \
    $$MC_ACCOUNT_DIR/json.h \
    $$MC_ACCOUNT_DIR/platform.h

SOURCES += \
    $$MC_ACCOUNT_DIR/account.cpp \
    $$MC_ACCOUNT_DIR/browser_glue.cpp \
    $$MC_ACCOUNT_DIR/contract.cpp \
    $$MC_ACCOUNT_DIR/crypto.cpp \
    $$MC_ACCOUNT_DIR/file_lock.cpp \
    $$MC_ACCOUNT_DIR/json.cpp \
    $$MC_ACCOUNT_DIR/renderer_glue.cpp

core_windows {
    SOURCES += $$MC_ACCOUNT_DIR/credential_store_win.cpp
    LIBS += -ladvapi32 -lbcrypt -lshell32 -lole32
}
core_mac {
    SOURCES += $$MC_ACCOUNT_DIR/credential_store_mac.cpp
    LIBS += -framework Security -framework CoreFoundation
}
core_linux {
    SOURCES += $$MC_ACCOUNT_DIR/credential_store_linux.cpp
    LIBS += -ldl
}

MC_OFFICE_VERSION = $$(OFFICE_VERSION)
isEmpty(MC_OFFICE_VERSION): MC_OFFICE_VERSION = 0.0.0-mc.0
MC_OFFICE_FLAVOR = $$(OFFICE_FLAVOR)
!equals(MC_OFFICE_FLAVOR, production): MC_OFFICE_FLAVOR = staging
DEFINES += MARKETCANOPY_OFFICE_VERSION=\\\"$$MC_OFFICE_VERSION\\\" MARKETCANOPY_OFFICE_FLAVOR=\\\"$$MC_OFFICE_FLAVOR\\\"
