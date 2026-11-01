# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME ComputerTests
  DEPENDS gui
  SOURCE ComputerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ScreenAlignmentTests
  DEPENDS gui
  SOURCE ScreenAlignmentTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
# The Windows test seeds a portable profile before initializing Settings.
set_target_properties(ScreenAlignmentTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/alignment-model"
)
