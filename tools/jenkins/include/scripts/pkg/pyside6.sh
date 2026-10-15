#!/bin/bash

# Install PySide6 and shiboken6 (with the shiboken6 generator, which builds
# Natron's Python bindings) for the Qt 6 of $QT6PREFIX.
# The pip wheels need glibc 2.28: built from source, with the libclang of
# $LLVM_PREFIX (see llvm.sh).
# see https://doc.qt.io/qtforpython-6.8/building_from_source/linux.html
# Installed in $SDK_HOME (like PySide2): the Python packages go to
# $SDK_HOME/lib/python3.x/site-packages/{PySide6,shiboken6,shiboken6_generator},
# the libraries to $SDK_HOME/lib, the CMake packages to $SDK_HOME/lib/cmake.
PYSIDE6_VERSION=6.8.4
PYSIDE6_TAR="pyside-setup-opensource-src-${PYSIDE6_VERSION}.tar.xz"
PYSIDE6_SITE="https://download.qt.io/official_releases/QtForPython/pyside6/PySide6-${PYSIDE6_VERSION}-src"
# Only the modules Natron's Python API and its users need.
# The .pyi type hints (for IDEs) are not generated: generate_pyi.py imports
# PySide6 from the build tree, where shiboken6 is not found.
PYSIDE6_MODULES="Core;Gui;Widgets;Network;OpenGL;OpenGLWidgets;Svg;SvgWidgets;Concurrent"

if download_step; then
    download "$PYSIDE6_SITE" "$PYSIDE6_TAR"
fi
PYSIDE6_INIT="$SDK_HOME/lib/python${PY3_VERSION_SHORT}/site-packages/PySide6/__init__.py"
if build_step && { force_build || { [ ! -x "$SDK_HOME/bin/shiboken6" ] || ! grep -qs "__version__ = \"${PYSIDE6_VERSION}\"" "$PYSIDE6_INIT"; }; }; then
    start_build
    untar "$SRC_PATH/$PYSIDE6_TAR"
    pushd "pyside-setup-opensource-src-${PYSIDE6_VERSION}"
    # QtExampleIcons (the icons of PySide's examples) does not find Python.h
    # in this build (SHIBOKEN_PYTHON_INCLUDE_DIRS is empty there): not built.
    sed -i '/add_subdirectory(qtexampleicons)/d' sources/pyside6/CMakeLists.txt
    mkdir build
    pushd build
    env LLVM_INSTALL_DIR="$LLVM_PREFIX" \
        LD_LIBRARY_PATH="$QT6PREFIX/lib:$LLVM_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        cmake .. -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$SDK_HOME" \
        -DCMAKE_PREFIX_PATH="$QT6PREFIX;$LLVM_PREFIX;$SDK_HOME" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$BF" -DCMAKE_CXX_FLAGS="$BF" \
        -DPython_EXECUTABLE="$PY3_EXE" \
        -DMODULES="$PYSIDE6_MODULES" \
        -DBUILD_TESTS=OFF \
        -DNO_QT_TOOLS=yes \
        -DFORCE_LIMITED_API=yes \
        -DDISABLE_PYI=yes
    # Retried: a crashing compiler (unstable build machine) loses only the
    # object it was compiling, ninja resumes from the others.
    for try in 1 2 3 4 5; do
        if env LLVM_INSTALL_DIR="$LLVM_PREFIX" \
            LD_LIBRARY_PATH="$QT6PREFIX/lib:$LLVM_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            cmake --build . --parallel "${MKJOBS}"; then
            break
        fi
        if [ "$try" -eq 5 ]; then
            echo "*** Error: PySide6 build failed $try times"
            exit 1
        fi
        echo "*** Warning: PySide6 build failed (try $try), retrying"
    done
    cmake --install .
    popd
    popd
    rm -rf "pyside-setup-opensource-src-${PYSIDE6_VERSION}"
    end_build
fi
