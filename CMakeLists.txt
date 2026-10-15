# ***** BEGIN LICENSE BLOCK *****
# This file is part of Natron <https://natrongithub.github.io/>,
# (C) 2018-2023 The Natron developers
# (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
#
# Natron is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Natron is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
# ***** END LICENSE BLOCK *****

cmake_minimum_required(VERSION 3.16.7)

project(Natron
    VERSION "2.5.0"
    DESCRIPTION "Open Source Compositing Software"
    HOMEPAGE_URL "https://natrongithub.github.io/"
    LANGUAGES CXX C
)

set(NATRON_QT_MAJOR 5 CACHE STRING "Qt major version to build with (5 or 6)")
set_property(CACHE NATRON_QT_MAJOR PROPERTY STRINGS 5 6)
if(NOT NATRON_QT_MAJOR MATCHES "^[56]$")
    message(FATAL_ERROR "NATRON_QT_MAJOR must be 5 or 6, not '${NATRON_QT_MAJOR}'")
endif()

if(NATRON_QT_MAJOR EQUAL 6)
    # Qt 6 requires C++17
    set(CMAKE_CXX_STANDARD 17)
else()
    set(CMAKE_CXX_STANDARD 14)
endif()

option(NATRON_SYSTEM_LIBS "use system versions of dependencies instead of bundled ones" OFF)
option(NATRON_BUILD_TESTS "build the Natron test suite" ON)

set(IS_DEBUG_BUILD OFF)
if(CMAKE_BUILD_TYPE MATCHES "^(debug|Debug|DEBUG)$")
    set(IS_DEBUG_BUILD ON)
    add_definitions(-DDEBUG)

    if(WIN32)
        # Debug builds need minimal optimizations to avoid excessive link times and link errors related to Eigen.
        add_compile_options(-O)
    endif()
else()
    add_definitions(-DQT_NO_DEBUG_OUTPUT)
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build." FORCE)
    message(STATUS "Setting build type to '${CMAKE_BUILD_TYPE}' as none was specified.")
endif()

find_package(Boost COMPONENTS headers serialization)
if(WIN32)
    # Prevent looking in the registry so that the MinGW python can be found.
    set(Python3_FIND_REGISTRY NEVER)
endif()
find_package(Python3 COMPONENTS Interpreter Development)
# The modules use the versionless Qt:: targets (Qt 5.15 and Qt 6).
if(NATRON_QT_MAJOR EQUAL 6)
    find_package(Qt6 6.5 CONFIG REQUIRED COMPONENTS Core Gui Network Widgets Concurrent OpenGL OpenGLWidgets)
else()
    find_package(Qt5 5.15 CONFIG REQUIRED COMPONENTS Core Gui Network Widgets Concurrent)
endif()
set(QT_VERSION_MAJOR ${NATRON_QT_MAJOR})

if(IS_DEBUG_BUILD AND WIN32)
    # Explicitly setting SHIBOKEN_PYTHON_LIBRARIES variable to avoid PYTHON_DEBUG_LIBRARY-NOTFOUND
    # link errors on Windows debug builds.
    set(SHIBOKEN_PYTHON_LIBRARIES ${Python3_LIBRARIES})
endif()

# Python bindings: PySide2/Shiboken2 with Qt 5, PySide6/Shiboken6 with Qt 6.
# NATRON_SHIBOKEN_LIB, NATRON_SHIBOKEN_GENERATOR and NATRON_PYSIDE_LIB are the
# targets to use, NATRON_PYSIDE_NAME is PySide2 or PySide6.
if(NATRON_QT_MAJOR EQUAL 6)
    find_package(Shiboken6 6.5 CONFIG REQUIRED COMPONENTS libshiboken)
    # since PySide 6.5 the generator comes in its own package
    find_package(Shiboken6Tools 6.5 CONFIG QUIET)
    find_package(PySide6 6.5 CONFIG REQUIRED COMPONENTS pyside6)
    set(NATRON_PYSIDE_NAME PySide6)
    set(NATRON_SHIBOKEN_LIB Shiboken6::libshiboken)
    set(NATRON_SHIBOKEN_GENERATOR Shiboken6::shiboken6)
    set(NATRON_PYSIDE_LIB PySide6::pyside6)
    set(NATRON_SHIBOKEN_NB_BOOL_OPTION "--use-isnull-as-nb-bool")
    set(NATRON_POST_SHIBOKEN ${CMAKE_SOURCE_DIR}/scripts/runPostShiboken6.sh)
else()
    find_package(Shiboken2 5.15 CONFIG REQUIRED COMPONENTS libshiboken2 shiboken2)
    find_package(PySide2 5.15 CONFIG REQUIRED COMPONENTS pyside2)
    set(NATRON_PYSIDE_NAME PySide2)
    set(NATRON_SHIBOKEN_LIB Shiboken2::libshiboken)
    set(NATRON_SHIBOKEN_GENERATOR Shiboken2::shiboken2)
    set(NATRON_PYSIDE_LIB PySide2::pyside2)
    set(NATRON_SHIBOKEN_NB_BOOL_OPTION "--use-isnull-as-nb_nonzero")
    set(NATRON_POST_SHIBOKEN ${CMAKE_SOURCE_DIR}/tools/utils/runPostShiboken2.sh)
endif()
if(NOT TARGET ${NATRON_SHIBOKEN_GENERATOR})
    message(FATAL_ERROR "The shiboken generator (${NATRON_SHIBOKEN_GENERATOR}) was not found")
endif()

if(IS_DEBUG_BUILD AND WIN32)
    # Remove NDEBUG from the Shiboken INTERFACE_COMPILE_DEFINITIONS so it is not inherited in debug builds.
    get_property(ShibokenInterfaceDefs TARGET ${NATRON_SHIBOKEN_LIB} PROPERTY INTERFACE_COMPILE_DEFINITIONS)
    list(REMOVE_ITEM ShibokenInterfaceDefs NDEBUG)
    set_property(TARGET ${NATRON_SHIBOKEN_LIB} PROPERTY INTERFACE_COMPILE_DEFINITIONS ShibokenInterfaceDefs)
endif()

get_target_property(PYSIDE_INCLUDE_DIRS ${NATRON_PYSIDE_LIB} INTERFACE_INCLUDE_DIRECTORIES)
# Qt include directories, passed to the shiboken generator
get_target_property(QTCORE_INCLUDE_DIRS Qt::Core INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(QTGUI_INCLUDE_DIRS Qt::Gui INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(QTWIDGETS_INCLUDE_DIRS Qt::Widgets INTERFACE_INCLUDE_DIRECTORIES)
foreach(dir ${QTCORE_INCLUDE_DIRS})
    if(dir MATCHES "(.*)/QtCore$")
        # the parent of QtCore, for <QtCore/...> includes
        list(APPEND QTCORE_INCLUDE_DIRS ${CMAKE_MATCH_1})
    endif()
endforeach()
# root of the Qt installation, for the macOS framework headers
set(NATRON_QT_DIR ${Qt${NATRON_QT_MAJOR}_DIR})

#Since in Natron and OpenFX all strings are supposed UTF-8 and that the constructor
#for QString(const char*) assumes ASCII strings, we may run into troubles
add_compile_definitions(QT_NO_CAST_FROM_ASCII)

if (NOT MSVC)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes" )
    add_compile_options(-Wall -Wextra -Wmissing-declarations -Wno-multichar -Winit-self -Wno-long-long
                        -Wvla -Wdate-time -Wshift-overflow=2
                        -Wduplicated-cond -Wno-stringop-overflow -Wno-format-overflow
                        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>)
endif()

if(NATRON_SYSTEM_LIBS)
    find_package(glog)
    find_package(Ceres)
endif()

if(UNIX AND NOT APPLE)
    find_package(ECM NO_MODULE)
    set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
    find_package(X11 REQUIRED)
    find_package(Wayland COMPONENTS Client Egl)
elseif(APPLE)
    enable_language(OBJCXX)
elseif(WIN32)
    enable_language(RC)
    add_compile_definitions(WINDOWS WIN32 _UNICODE UNICODE NOMINMAX QHTTP_SERVER_STATIC)
    add_compile_definitions(COMPILED_FROM_DSP XML_STATIC) # for expat
    if (NOT MSVC)
        add_compile_options(-mwindows -municode -mthreads)
        add_link_options(-mwindows -municode)
        set(CMAKE_RC_COMPILER_INIT windres)
        set(CMAKE_RC_COMPILE_OBJECT
            "<CMAKE_RC_COMPILER> <FLAGS> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
    endif()
endif()

include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

enable_testing()

add_subdirectory(libs)

add_subdirectory(HostSupport)

add_subdirectory(Engine)

add_subdirectory(PythonBin)

add_subdirectory(Renderer)

add_subdirectory(Custom)

add_subdirectory(Gui)

if(NATRON_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

add_subdirectory(App)
