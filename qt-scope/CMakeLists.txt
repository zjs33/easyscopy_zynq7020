cmake_minimum_required(VERSION 3.10)

project(ac880_zynq_scope VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(ZYNQ_SCOPE_FORCE_QT5 "Build with Qt 5 even when a host Qt 6 is visible" OFF)
set(ZYNQ_SCOPE_QT5_VERSION "5.12.8" CACHE STRING
    "Qt 5 version required when building ZynqScope")
option(ZYNQ_SCOPE_QT5_EXACT_VERSION
    "Require the exact configured Qt 5 version" OFF)

set(ZYNQ_SCOPE_QT5_FIND_VERSION_ARGS ${ZYNQ_SCOPE_QT5_VERSION})
if(ZYNQ_SCOPE_QT5_EXACT_VERSION)
    list(APPEND ZYNQ_SCOPE_QT5_FIND_VERSION_ARGS EXACT)
endif()

if(ZYNQ_SCOPE_FORCE_QT5)
    find_package(Qt5 ${ZYNQ_SCOPE_QT5_FIND_VERSION_ARGS}
            REQUIRED COMPONENTS Core Gui Widgets Network)
    set(ZYNQ_SCOPE_QT_MAJOR_VERSION 5)
else()
    find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Network)
    if(Qt6_FOUND)
        set(ZYNQ_SCOPE_QT_MAJOR_VERSION 6)
    else()
        find_package(Qt5 ${ZYNQ_SCOPE_QT5_FIND_VERSION_ARGS}
            REQUIRED COMPONENTS Core Gui Widgets Network)
        set(ZYNQ_SCOPE_QT_MAJOR_VERSION 5)
    endif()
endif()

if(ZYNQ_SCOPE_QT_MAJOR_VERSION EQUAL 6)
    set(CMAKE_CXX_STANDARD 17)
else()
    set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
