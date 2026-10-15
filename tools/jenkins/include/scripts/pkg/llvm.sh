#!/bin/bash

# Install LLVM/clang, for libclang: the shiboken6 generator of PySide6 parses
# C++ with it (PySide 6.8 needs libclang 13 or newer, 18 is recommended).
# Qt's prebuilt libclang for RHEL 7 stops at version 12, so it is built here.
# shiboken6 finds it with find_package(Clang CONFIG): a full install (with the
# CMake packages and the static LLVM/clang libraries they list) is needed. The
# LLVM tools are not built.
# see https://llvm.org/docs/CMake.html
LLVM_VERSION=18.1.8
LLVM_TAR="llvm-project-${LLVM_VERSION}.src.tar.xz"
LLVM_SITE="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}"
if download_step; then
    download "$LLVM_SITE" "$LLVM_TAR"
fi
if build_step && { force_build || { [ ! -s "$LLVM_PREFIX/lib/libclang.so" ] || [ "$("$LLVM_PREFIX/bin/clang" --version | sed -n 's/.*clang version \([0-9.]*\).*/\1/p')" != "$LLVM_VERSION" ]; }; }; then
    start_build
    untar "$SRC_PATH/$LLVM_TAR"
    pushd "llvm-project-${LLVM_VERSION}.src"
    mkdir build
    pushd build
    cmake ../llvm -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$LLVM_PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$BF" -DCMAKE_CXX_FLAGS="$BF" \
        -DLLVM_ENABLE_PROJECTS=clang \
        -DLLVM_TARGETS_TO_BUILD=X86 \
        -DLLVM_BUILD_TOOLS=OFF \
        -DLLVM_BUILD_UTILS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_DOCS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF \
        -DCLANG_INCLUDE_DOCS=OFF \
        -DCLANG_ENABLE_ARCMT=OFF \
        -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
        -DLLVM_PARALLEL_LINK_JOBS=1 \
        -DPython3_EXECUTABLE="$PY3_EXE"
    ninja -j"${MKJOBS}" install
    popd
    popd
    rm -rf "llvm-project-${LLVM_VERSION}.src"
    end_build
fi
