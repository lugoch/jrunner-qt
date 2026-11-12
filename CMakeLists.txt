cmake_minimum_required(VERSION 3.20)
project(jrunner-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(JRUNNER_WITH_LAYER_SHELL "Enable LayerShellQt integration on Wayland when available" ON)
option(JRUNNER_REQUIRE_LAYER_SHELL "Fail if LayerShellQt is required but not found" OFF)

include(CTest)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(JRUNNER_LINUX TRUE)
else()
  set(JRUNNER_LINUX FALSE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
if(JRUNNER_LINUX)
  find_package(Qt6 QUIET COMPONENTS WaylandClient)
  if(JRUNNER_WITH_LAYER_SHELL)
    find_package(LayerShellQt QUIET)
    if(JRUNNER_REQUIRE_LAYER_SHELL AND NOT LayerShellQt_FOUND)
      message(FATAL_ERROR "LayerShellQt required but not found")
    endif()
  endif()
endif()

add_subdirectory(qt)
