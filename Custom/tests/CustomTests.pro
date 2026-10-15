# Unit tests of Custom/ (Qt only, no Natron engine), standalone:
#   qmake Custom/tests/CustomTests.pro && make && ./CustomTests
# (The CMake build also checks NdiOutput against a fake NDI runtime.)

TEMPLATE = app
TARGET = CustomTests
CONFIG += console c++14
CONFIG -= app_bundle
QT += core gui network
unix: LIBS += -lpthread

INCLUDEPATH += $$PWD/../.. $$PWD/../../Tests/google-test/include $$PWD/../../Tests/google-test

CUSTOM_DIRS = state server scene util ndi sdi scoreboard
for(dir, CUSTOM_DIRS) {
    SOURCES += $$files($$PWD/../$$dir/*.cpp)
    HEADERS += $$files($$PWD/../$$dir/*.h)
}

SOURCES += \
    $$PWD/../../Tests/google-test/src/gtest-all.cc \
    $$PWD/TestUtil.cpp \
    $$PWD/main.cpp \
    $$files($$PWD/*_Test.cpp)

HEADERS += $$PWD/TestUtil.h
