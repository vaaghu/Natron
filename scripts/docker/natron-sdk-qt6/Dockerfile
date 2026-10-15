# Natron SDK for Qt 6 builds: the official natrongithub/natron-sdk image
# (CentOS 7, devtoolset-11, Python 3.10, Qt 4/5) plus, built by the SDK's
# own recipes (tools/jenkins/include/scripts/pkg):
#   llvm.sh     LLVM/clang 18 in /opt/Natron-sdk/llvm, for libclang
#   qt6.sh      Qt 6.8 (qtbase, qtsvg, qtimageformats) in /opt/Natron-sdk/qt6
#   pyside6.sh  PySide6 and shiboken6 6.8 in /opt/Natron-sdk
# Each step is its own layer: changing one recipe rebuilds from that step.
#
# Build (the context is tools/jenkins, for the recipes):
#   scripts/natron-build.sh sdk --qt6
#   (or: docker build -t natron-sdk-qt6:latest -f scripts/docker/natron-sdk-qt6/Dockerfile tools/jenkins)
# Used by: scripts/natron-build.sh --qt6
# A full build takes hours (LLVM is most of it). The steps' logs stream to
# the build output, and stay in /opt/Natron-sdk/var/log/Natron-Linux-x86_64-SDK.

FROM natrongithub/natron-sdk:latest

# Parallel compile jobs.
ARG MKJOBS=2

# CentOS 7 is end-of-life: its packages are on vault.centos.org, EPEL 7's in
# the Fedora archive. The xcb-util libraries are for Qt's xcb platform
# plug-in (xcb-util-cursor is only in EPEL).
RUN set -e; \
    printf '%s\n' \
        '[c7-vault-os]' 'name=CentOS 7.9 os' 'baseurl=http://vault.centos.org/7.9.2009/os/$basearch/' 'gpgcheck=0' \
        '[c7-vault-updates]' 'name=CentOS 7.9 updates' 'baseurl=http://vault.centos.org/7.9.2009/updates/$basearch/' 'gpgcheck=0' \
        '[epel7-archive]' 'name=EPEL 7 archive' 'baseurl=https://dl.fedoraproject.org/pub/archive/epel/7/$basearch/' 'gpgcheck=0' \
        > /etc/yum.repos.d/natron-qt6.repo; \
    # (yum needs the system Python: not the SDK one, first on PATH and LD_LIBRARY_PATH)
    env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin yum -y --disablerepo='*' --enablerepo='c7-vault-*,epel7-archive' install \
        xcb-util-devel xcb-util-image-devel xcb-util-keysyms-devel \
        xcb-util-renderutil-devel xcb-util-wm-devel xcb-util-cursor-devel \
        libxkbcommon-x11-devel; \
    env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin yum clean all

# The SDK build script with the layout it expects (common.sh next to it,
# include/ for the patches, pkg/ for the recipes), in its own workspace so
# that /home/tmp and /home/src stay free for the Natron builds.
WORKDIR /home/sdk
COPY common.sh compiler-common.sh include/scripts/build-Linux-sdk.sh ./
COPY include/patches/ include/patches/
# Sourced for the Python variables (PY3_EXE...) the recipes use; Python itself
# is in the SDK already.
COPY include/scripts/pkg/python3.sh pkg/

# Runs one step of build-Linux-sdk.sh on top of the existing SDK. The step
# writes its output to its log file: streamed here, so that it shows in the
# docker build output.
RUN printf '%s\n' '#!/bin/bash' \
        'source scl_source enable devtoolset-11' \
        'cd /home/sdk' \
        'log="/opt/Natron-sdk/var/log/Natron-Linux-x86_64-SDK/$1.log"' \
        'mkdir -p "${log%/*}" && : > "$log"' \
        'tail -n +1 -F "$log" & tailpid=$!' \
        'env WORKSPACE=/home/sdk/work CENTOS=7 DTS=11 MKJOBS="$MKJOBS" ONLY_STEPS="$1" ./build-Linux-sdk.sh' \
        'status=$?' \
        'sleep 1; kill $tailpid' \
        'rm -rf /home/sdk/work' \
        'exit $status' \
        > /usr/local/bin/natron-sdk-step && chmod +x /usr/local/bin/natron-sdk-step

COPY include/scripts/pkg/llvm.sh pkg/
RUN MKJOBS=${MKJOBS} natron-sdk-step llvm

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
