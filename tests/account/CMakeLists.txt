# Market Canopy Office: tests for the account module (ADR 0524). They build the
# module's plain C++ sources, with no CEF and no ONLYOFFICE checkout, and run
# them against contracts/fixtures. See README.md.
#
# Copyright (C) 2026 Market Canopy LLC
# SPDX-License-Identifier: AGPL-3.0-only

cmake_minimum_required(VERSION 3.20)
project(marketcanopy_account_tests CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MODULE_DIR "${REPO_ROOT}/branding/marketcanopy/add/desktop-sdk/ChromiumBasedEditors/lib/src/marketcanopy")
set(FIXTURES_DIR "${REPO_ROOT}/contracts/fixtures")

file(GLOB_RECURSE FIXTURE_FILES RELATIVE "${FIXTURES_DIR}" "${FIXTURES_DIR}/*.json")
list(SORT FIXTURE_FILES)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fixture_list.h"
     "#define MC_FIXTURES_DIR \"${FIXTURES_DIR}\"\n#define MC_FIXTURE_FILES \"${FIXTURE_FILES}\"\n")

if(MSVC)
  add_compile_options(/W4 /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

add_library(account STATIC
  "${MODULE_DIR}/account.cpp"
  "${MODULE_DIR}/contract.cpp"
  "${MODULE_DIR}/crypto.cpp"
  "${MODULE_DIR}/json.cpp"
  "${MODULE_DIR}/file_lock.cpp")
if(WIN32)
  target_sources(account PRIVATE "${MODULE_DIR}/credential_store_win.cpp")
  target_link_libraries(account PUBLIC advapi32 bcrypt shell32 ole32)
elseif(APPLE)
  target_sources(account PRIVATE "${MODULE_DIR}/credential_store_mac.cpp")
  target_link_libraries(account PUBLIC "-framework Security" "-framework CoreFoundation")
else()
  target_sources(account PRIVATE "${MODULE_DIR}/credential_store_linux.cpp")
  target_link_libraries(account PUBLIC ${CMAKE_DL_LIBS})
  find_package(Threads REQUIRED)
  target_link_libraries(account PUBLIC Threads::Threads)
endif()
target_include_directories(account PUBLIC "${MODULE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(account PUBLIC
  MARKETCANOPY_OFFICE_VERSION="10.0.0-mc.3" MARKETCANOPY_OFFICE_FLAVOR="production")

enable_testing()
foreach(name fixture_tests trace_tests os_store_tests)
  add_executable(${name} "${name}.cpp")
  target_link_libraries(${name} PRIVATE account)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
