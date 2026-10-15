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

file(GLOB NatronEngine_HEADERS ../Engine/*.h)
file(GLOB NatronGui_HEADERS *.h)
file(GLOB NatronGui_SOURCES *.cpp)

set(PYGUI_OUT ${CMAKE_CURRENT_BINARY_DIR}/Qt${QT_VERSION_MAJOR})
set(PYGUI_INCLUDE_DIRS . .. ../Engine ../Global ../libs/OpenFX/include)
list(APPEND PYGUI_INCLUDE_DIRS ${PYSIDE_INCLUDE_DIRS} ${PYSIDE_INCLUDE_DIRS}/QtCore
    ${PYSIDE_INCLUDE_DIRS}/QtGui ${PYSIDE_INCLUDE_DIRS}/QtWidgets)
list(APPEND PYGUI_INCLUDE_DIRS ${QTCORE_INCLUDE_DIRS} ${QTGUI_INCLUDE_DIRS}
    ${QTWIDGETS_INCLUDE_DIRS} ${Python3_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
if(NATRON_QT_MAJOR EQUAL 6)
    # QOpenGLWidget and the QOpenGL* classes moved to their own modules in Qt 6
    get_target_property(QTOPENGL_INCLUDE_DIRS Qt::OpenGL INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(QTOPENGLWIDGETS_INCLUDE_DIRS Qt::OpenGLWidgets INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND PYGUI_INCLUDE_DIRS ${QTOPENGL_INCLUDE_DIRS} ${QTOPENGLWIDGETS_INCLUDE_DIRS})
endif()
list(TRANSFORM PYGUI_INCLUDE_DIRS PREPEND "-I" OUTPUT_VARIABLE PYGUI_INCS)
set(PYGUI_HEADER ${NATRON_PYSIDE_NAME}_Gui_Python.h)
set(POST_SHIBOKEN ${NATRON_POST_SHIBOKEN})

set(shiboken_args
    "--enable-parent-ctor-heuristic" "${NATRON_SHIBOKEN_NB_BOOL_OPTION}"
    "--avoid-protected-hack" "--enable-pyside-extensions"
    ${PYGUI_INCS}
    "-T../Engine" "-T${PYSIDE_TYPESYSTEMS}" "--output-directory=${PYGUI_OUT}"
    "${PYGUI_HEADER}" "typesystem_natronGui.xml")
if(APPLE)
   list(APPEND shiboken_args -I${NATRON_QT_DIR}/../../../include)
endif()

execute_process(COMMAND ${Python3_EXECUTABLE} ../tools/utils/sourceList.py typesystem_natronGui.xml ${PYGUI_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE PyGui_SOURCES)
set_property(SOURCE ${PyGui_SOURCES} PROPERTY SKIP_AUTOGEN ON)
add_custom_command(OUTPUT ${PyGui_SOURCES}
    COMMAND ${NATRON_SHIBOKEN_GENERATOR} ${shiboken_args}
    COMMAND bash ${POST_SHIBOKEN} ${PYGUI_OUT}/NatronGui natrongui
    DEPENDS ${PYGUI_HEADER} ${NatronEngine_HEADERS} ${NatronGui_HEADERS} ${POST_SHIBOKEN} typesystem_natronGui.xml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(APPLE)
    list(APPEND NatronGui_SOURCES QtMac.mm TaskBarMac.mm)
endif()

list(APPEND NatronGui_SOURCES
    ${NatronGui_SOURCES}
    ${PyGui_SOURCES}
    GuiResources.qrc
)
add_library(NatronGui STATIC ${NatronGui_HEADERS} ${NatronGui_SOURCES})
target_link_libraries(NatronGui
    PUBLIC
        ${NATRON_SHIBOKEN_LIB}
        ${NATRON_PYSIDE_LIB}
        Qt::Core
        Qt::Concurrent
        Qt::Network
        Qt::Gui
        Qt::Widgets
        Python3::Module
        NatronEngine
    PRIVATE
        qhttpserver
        NatronCustom
)
if(NATRON_QT_MAJOR EQUAL 6)
    # QOpenGLWidget and the QOpenGL* classes moved to their own modules in Qt 6
    target_link_libraries(NatronGui PUBLIC Qt::OpenGL Qt::OpenGLWidgets)
endif()
target_include_directories(NatronGui
    PRIVATE
        ${PYSIDE_INCLUDE_DIRS}/QtCore
        ${PYSIDE_INCLUDE_DIRS}/QtGui
        ${PYSIDE_INCLUDE_DIRS}/QtWidgets
        .
        ..
        ../Engine
        ../Global
        ../libs/SequenceParsing
        ../libs/qhttpserver/src
        ${CMAKE_BINARY_DIR}/Engine/Qt${QT_VERSION_MAJOR}/NatronEngine
)
target_compile_definitions(NatronGui
    PRIVATE
        NATRON_CUSTOM_BUILD_USER_TOKEN=${BUILD_USER_NAME}
        ${NATRON_VERSION_DEFINITIONS}
        QT_NO_KEYWORDS
)
