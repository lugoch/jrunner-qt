find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)

add_executable(jrunner-core-tests
  core_logic_tests.cpp
  ../src/core/models.cpp
  ../src/core/project_scanner.cpp
  ../src/core/project_cache.cpp
  ../src/core/task_planner.cpp
  ../src/core/process_task.cpp
  ../src/core/jdk_resolver.cpp
  ../src/core/git_info.cpp
  ../src/core/app_config.cpp
  ../src/core/shell_env.cpp
  ../src/core/file_models.cpp
  ../src/core/debug_log.cpp
)
target_link_libraries(jrunner-core-tests PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
target_include_directories(jrunner-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME jrunner-core-logic COMMAND jrunner-core-tests)

get_target_property(JRUNNER_CORE_TEST_SOURCES jrunner-core-tests SOURCES)
list(REMOVE_ITEM JRUNNER_CORE_TEST_SOURCES core_logic_tests.cpp)
add_executable(jrunner-ui-tests
  ui_interaction_tests.cpp
  ${JRUNNER_CORE_TEST_SOURCES}
  ../src/app/app_store.cpp
  ../src/app/mcp_control_server.cpp
  ../src/app/main_window.cpp
  ../src/ui/project_sidebar.cpp
  ../src/ui/module_panel.cpp
  ../src/ui/log_console.cpp
  ../src/ui/full_log_dialog.cpp
  ../src/ui/file_browser.cpp
  ../src/ui/code_editor.cpp
  ../src/ui/syntax_highlighter.cpp
  ../src/ui/settings_dialog.cpp
  ../src/ui/theme.cpp
  ../src/ui/widgets.cpp
)
target_link_libraries(jrunner-ui-tests PRIVATE Qt6::Core Qt6::Widgets Qt6::Test Qt6::Concurrent Qt6::Network)
target_include_directories(jrunner-ui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME jrunner-ui-interaction COMMAND jrunner-ui-tests)
set_tests_properties(jrunner-ui-interaction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=wayland")
