# Explorer context menu helper: lets the user choose setting overrides for a
# single DOSBox run before starting it with the right-clicked item.

configure_file(launcher.rc.in launcher.rc @ONLY)

add_executable(dosbox_launcher WIN32
  main.cpp
  command_line.cpp
  launcher_dialog.cpp
  settings_schema.cpp
  win32_utils.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc
  dosbox_launcher.manifest
)

# Put the launcher next to the DOSBox executable it starts
set_target_properties(dosbox_launcher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_definitions(dosbox_launcher PRIVATE
  NOMINMAX
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
)

target_link_libraries(dosbox_launcher PRIVATE
  project_headers
  json
  comctl32
  uxtheme
)

if (OPT_TESTS)
  target_sources(dosbox_tests PRIVATE
    command_line.cpp
    settings_schema.cpp
  )
endif()
