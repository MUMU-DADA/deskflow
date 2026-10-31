# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME GameProbeOptionsTests
  DEPENDS Qt6::Core
  SOURCE GameProbeOptionsTests.cpp
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(GameProbeOptionsTests PRIVATE "${PROJECT_SOURCE_DIR}/src/apps/deskflow-game-probe")
if(APPLE)
  find_library(PROBE_TEST_COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(GameProbeOptionsTests ${PROBE_TEST_COREFOUNDATION})
endif()
