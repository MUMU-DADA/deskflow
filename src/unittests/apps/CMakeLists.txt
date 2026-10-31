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

  set(core_source_dir "${PROJECT_SOURCE_DIR}/src/apps/deskflow-core")
  get_target_property(core_libraries ${CMAKE_PROJECT_NAME}-core LINK_LIBRARIES)
  add_library(MacCoreEntryForTests OBJECT "${core_source_dir}/deskflow-core.cpp")
  target_compile_definitions(MacCoreEntryForTests PRIVATE main=deskflowCoreMain)
  target_link_libraries(MacCoreEntryForTests PRIVATE ${core_libraries})

  create_test(
    NAME MacCoreApplicationTests
    DEPENDS app
    SOURCE MacCoreApplicationTests.mm
    LIBS ${core_libraries}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  target_sources(MacCoreApplicationTests PRIVATE
    MacCoreApplicationTests.h
    "${core_source_dir}/MacCoreApplication.mm"
    "${core_source_dir}/CoreArgParser.cpp"
    $<TARGET_OBJECTS:MacCoreEntryForTests>
  )
  target_include_directories(MacCoreApplicationTests PRIVATE "${core_source_dir}")

  # Model a secondary executable inside a normal GUI bundle. The test host has
  # no LSUIElement/LSBackgroundOnly flags that could mask the Qt regression.
  set(policy_host "${CMAKE_CURRENT_BINARY_DIR}/CorePolicyHost.app/Contents")
  file(MAKE_DIRECTORY "${policy_host}/MacOS")
  configure_file(CorePolicyHost.plist.in "${policy_host}/Info.plist" @ONLY)
  set_target_properties(MacCoreApplicationTests PROPERTIES
    OUTPUT_NAME "${CMAKE_PROJECT_NAME}-core"
    RUNTIME_OUTPUT_DIRECTORY "${policy_host}/MacOS"
  )
  get_target_property(core_info_plist ${CMAKE_PROJECT_NAME}-core DESKFLOW_CORE_INFO_PLIST)
  target_link_options(MacCoreApplicationTests PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${core_info_plist}")
  set_property(TARGET MacCoreApplicationTests APPEND PROPERTY LINK_DEPENDS "${core_info_plist}")
  add_custom_command(TARGET MacCoreApplicationTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MacCoreApplicationTests>" "${policy_host}/MacOS/PolicyHost"
    VERBATIM
  )
  set_tests_properties(MacCoreApplicationTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
