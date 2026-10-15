# Natron SDK for Qt 6 builds: natron-sdk-qt6-llvm (Dockerfile.llvm: the
# official natrongithub/natron-sdk image, CentOS 7, devtoolset-11, Python 3.10,
# Qt 4/5, plus LLVM/clang 18 in /opt/Natron-sdk/llvm) and, built by the SDK's
# own recipes (tools/jenkins/include/scripts/pkg):
#   qt6.sh      Qt 6.8 (qtbase, qtsvg, qtimageformats) in /opt/Natron-sdk/qt6
#   pyside6.sh  PySide6 and shiboken6 6.8 in /opt/Natron-sdk
# Each step is its own layer: changing one recipe rebuilds from that step.
#
# Build (the context is tools/jenkins, for the recipes):
#   scripts/natron-build.sh sdk --qt6
#   (or: docker build -t natron-sdk-qt6:latest -f scripts/docker/natron-sdk-qt6/Dockerfile tools/jenkins,
#   once natron-sdk-qt6-llvm:latest exists)
# Used by: scripts/natron-build.sh --qt6
# The steps' logs stream to the build output, and stay in
# /opt/Natron-sdk/var/log/Natron-Linux-x86_64-SDK.

FROM natron-sdk-qt6-llvm:latest

# Parallel compile jobs.
ARG MKJOBS=2

# The current build scripts (natron-sdk-qt6-llvm has those it was built with).
WORKDIR /home/sdk
COPY common.sh compiler-common.sh include/scripts/build-Linux-sdk.sh ./
COPY include/patches/ include/patches/
COPY include/scripts/pkg/python3.sh pkg/

COPY include/scripts/pkg/qt6.sh pkg/
RUN MKJOBS=${MKJOBS} natron-sdk-step qt6

COPY include/scripts/pkg/pyside6.sh pkg/
RUN MKJOBS=${MKJOBS} natron-sdk-step pyside6

# The shiboken6 generator finds clang's built-in headers through
# LLVM_INSTALL_DIR, and loads libclang from there.
ENV LLVM_INSTALL_DIR=/opt/Natron-sdk/llvm \
    QT6PREFIX=/opt/Natron-sdk/qt6

# Check: the generator runs, PySide6 imports with the SDK's Python.
RUN set -e; \
    LD_LIBRARY_PATH=/opt/Natron-sdk/qt6/lib:/opt/Natron-sdk/llvm/lib:$LD_LIBRARY_PATH /opt/Natron-sdk/bin/shiboken6 --version; \
    LD_LIBRARY_PATH=/opt/Natron-sdk/qt6/lib:$LD_LIBRARY_PATH /opt/Natron-sdk/bin/python3 -c \
        'import PySide6, PySide6.QtCore, PySide6.QtGui, PySide6.QtWidgets; print("PySide6", PySide6.__version__, "Qt", PySide6.QtCore.qVersion())'

WORKDIR /home
