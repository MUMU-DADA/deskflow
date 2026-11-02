# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME ActivitySyncSettingsTests
  DEPENDS gui
  LIBS common Qt6::Widgets
  SOURCE ActivitySyncSettingsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

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

create_test(
  NAME ScreenCalibrationOverlayTests
  DEPENDS gui
  SOURCE ScreenCalibrationOverlayTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_target_properties(ScreenCalibrationOverlayTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/calibration-overlay"
)

# This deliberately uses Cocoa even when the remaining suite uses offscreen:
# native first-show frame conversion is the behavior under test.
if(APPLE)
  create_test(
    NAME ScreenCalibrationNativeSmoke
    DEPENDS gui
    SOURCE ScreenCalibrationNativeSmoke.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
  )
  set_tests_properties(ScreenCalibrationNativeSmoke PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 10
  )
endif()

create_test(
  NAME ScreenCalibrationTests
  DEPENDS gui
  SOURCE ScreenCalibrationTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_target_properties(ScreenCalibrationTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/calibration-dialog"
)
