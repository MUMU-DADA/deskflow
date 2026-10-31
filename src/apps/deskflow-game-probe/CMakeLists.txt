# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

set(target deskflow-game-probe)

# Keep this a console program on Windows and an agent-only bundle on macOS.
# It calls the native detector without constructing a Deskflow core or GUI.
add_executable(${target} deskflow-game-probe.cpp ProbeOptions.h ProbeWait.h)
target_link_libraries(${target} PRIVATE platform Qt6::Core)

if(APPLE)
  find_library(PROBE_COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(${target} PRIVATE ${PROBE_COREFOUNDATION})
  set_target_properties(${target} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/probe.plist.in"
    INSTALL_RPATH "@loader_path/../Frameworks"
  )
endif()

# The probe is packaged separately by the diagnostic workflow; do not install it
# into the user's existing Deskflow application or configuration directories.
