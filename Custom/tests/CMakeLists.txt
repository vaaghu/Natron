# Unit tests of Custom/ (Qt only, no Natron engine).
# Part of the Natron build with NATRON_BUILD_TESTS, or on their own:
#   cmake -S Custom/tests -B <build dir> && cmake --build <build dir> && <build dir>/CustomTests
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.10)
    project(NatronCustomTests CXX)
    set(NATRON_QT_MAJOR 5 CACHE STRING "Qt major version to build with (5 or 6)")
    if(NATRON_QT_MAJOR EQUAL 6)
        set(CMAKE_CXX_STANDARD 17)
    else()
        set(CMAKE_CXX_STANDARD 14)
    endif()
    set(CMAKE_AUTOMOC ON)
    find_package(Qt${NATRON_QT_MAJOR} REQUIRED COMPONENTS Core Gui Network)
    add_subdirectory(.. NatronCustom)
    enable_testing()
endif()

set(NATRON_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Threads REQUIRED)

file(GLOB CustomTests_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_Test.cpp)
add_executable(CustomTests
    ${NATRON_ROOT}/Tests/google-test/src/gtest-all.cc
    TestUtil.h
    TestUtil.cpp
    main.cpp
    ${CustomTests_TESTS}
)
target_include_directories(CustomTests
    PRIVATE
        ${NATRON_ROOT}/Tests/google-test/include
        ${NATRON_ROOT}/Tests/google-test
)
target_link_libraries(CustomTests
    PRIVATE
        NatronCustom
        Qt::Core
        Qt::Gui
        Qt::Network
        Threads::Threads
)

# A fake NDI runtime (libndi.so) that NdiOutput loads instead of the real one.
if(UNIX AND NOT APPLE)
    add_library(CustomTestsFakeNdi SHARED FakeNdi.cpp)
    set_target_properties(CustomTestsFakeNdi PROPERTIES
        OUTPUT_NAME ndi
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fakendi)
    add_dependencies(CustomTests CustomTestsFakeNdi)
    target_compile_definitions(CustomTests PRIVATE FAKE_NDI_DIR="${CMAKE_CURRENT_BINARY_DIR}/fakendi")
endif()

add_test(NAME CustomTests COMMAND CustomTests)
