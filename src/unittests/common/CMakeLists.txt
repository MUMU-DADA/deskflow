# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME VolumeSyncTests
  DEPENDS common
  SOURCE VolumeSyncTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)

create_test(
  NAME SettingsTests
  DEPENDS common
  SOURCE SettingsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)

create_test(
  NAME LogLevelTests
  DEPENDS common
  SOURCE LogLevelTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)

create_test(
  NAME I18NTests
  DEPENDS common
  SOURCE I18NTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)
target_compile_definitions(I18NTests PRIVATE DESKFLOW_TEST_TRANSLATIONS_DIR="${CMAKE_BINARY_DIR}/translations")

create_test(
  NAME KeySequenceTests
  DEPENDS common
  SOURCE KeySequenceTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)
