set(JRUNNER_QT_SOURCES
  src/main.cpp
  src/app/main_window.cpp
  src/app/main_window.h
  src/app/app_store.cpp
  src/app/app_store.h
  src/app/mcp_control_server.cpp
  src/app/mcp_control_server.h
  src/core/models.cpp
  src/core/models.h
  src/core/project_scanner.cpp
  src/core/project_scanner.h
  src/core/project_cache.cpp
  src/core/project_cache.h
  src/core/task_planner.cpp
  src/core/task_planner.h
  src/core/process_task.cpp
  src/core/process_task.h
  src/core/jdk_resolver.cpp
  src/core/jdk_resolver.h
  src/core/git_info.cpp
  src/core/git_info.h
  src/core/app_config.cpp
  src/core/app_config.h
  src/core/shell_env.cpp
  src/core/shell_env.h
  src/core/file_models.cpp
  src/core/file_models.h
  src/core/debug_log.cpp
  src/core/debug_log.h
  src/ui/theme.cpp
  src/ui/theme.h
  src/ui/widgets.cpp
  src/ui/widgets.h
  src/ui/project_sidebar.cpp
  src/ui/project_sidebar.h
  src/ui/module_panel.cpp
  src/ui/module_panel.h
  src/ui/log_console.cpp
  src/ui/log_console.h
  src/ui/full_log_dialog.cpp
  src/ui/full_log_dialog.h
  src/ui/file_browser.cpp
  src/ui/file_browser.h
  src/ui/code_editor.cpp
  src/ui/code_editor.h
  src/ui/syntax_highlighter.cpp
  src/ui/syntax_highlighter.h
  src/ui/settings_dialog.cpp
  src/ui/settings_dialog.h
)

qt_add_executable(jrunner
  ${JRUNNER_QT_SOURCES}
)

target_include_directories(jrunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jrunner PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Network)

# stdio MCP 桥:供外部 agent 通过 MCP 控制运行中的 jrunner
add_executable(jrunner-mcp src/mcp/jrunner_mcp.cpp)
target_link_libraries(jrunner-mcp PRIVATE Qt6::Core Qt6::Network)

if(JRUNNER_LINUX AND TARGET Qt6::WaylandClient)
  target_link_libraries(jrunner PRIVATE Qt6::WaylandClient)
  target_compile_definitions(jrunner PRIVATE JRUNNER_HAVE_WAYLAND_CLIENT)
endif()
if(JRUNNER_LINUX AND LayerShellQt_FOUND)
  target_link_libraries(jrunner PRIVATE LayerShellQt::Interface)
  target_compile_definitions(jrunner PRIVATE JRUNNER_HAVE_LAYERSHELLQT)
  message(STATUS "jrunner: LayerShellQt found (available for future overlay integration)")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" JRUNNER_VERSION_TEXT)
string(STRIP "${JRUNNER_VERSION_TEXT}" JRUNNER_VERSION_TEXT)
target_compile_definitions(jrunner PRIVATE JRUNNER_VERSION="${JRUNNER_VERSION_TEXT}")

include(GNUInstallDirs)
install(TARGETS jrunner jrunner-mcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(JRUNNER_LINUX)
  configure_file(data/jrunner.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/jrunner.desktop @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/jrunner.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/icons/hicolor/scalable/apps/jrunner.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
