# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ScreenAlignmentDialogTests
  DEPENDS gui
  SOURCE ScreenAlignmentDialogTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_target_properties(ScreenAlignmentDialogTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/alignment-dialog"
)
target_include_directories(ScreenAlignmentDialogTests PRIVATE "${CMAKE_SOURCE_DIR}/src/lib/gui")
