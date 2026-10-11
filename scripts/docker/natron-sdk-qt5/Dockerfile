# Natron SDK for Qt 5 builds: the official natrongithub/natron-sdk image
# (Qt 5.15, Python 3.10, PySide2) plus the shiboken2 generator, which Qt 5
# builds need to generate Natron's Python bindings and the official image
# lacks (tools/jenkins/include/scripts/pkg/pyside2.sh installs PySide2 from
# pip without it).
#
# Build: scripts/natron-build.sh sdk
#   (or: docker build -t natron-sdk-qt5:latest scripts/docker/natron-sdk-qt5)
# Used by: scripts/natron-build.sh --qt5

FROM natrongithub/natron-sdk:latest

# Same version as the PySide2 of the SDK.
ARG SHIBOKEN2_VERSION=5.15.2.1
ARG GENERATOR=/opt/Natron-sdk/lib/python3.10/site-packages/shiboken2_generator

# 1. The generator wheel from Qt.
# 2. Its own Qt libraries need ICU 56, which the SDK does not have: use the
#    SDK's Qt 5 instead (as pyside2.sh does for PySide2).
# 3. A shiboken2 command on the build scripts' PATH ($SDK_HOME/bin), with the
#    SDK's ICU, then check that it runs.
RUN set -e; \
    /opt/Natron-sdk/bin/pip3.10 install --no-cache-dir \
        --index-url=https://download.qt.io/official_releases/QtForPython/ \
        --trusted-host download.qt.io \
        shiboken2-generator==${SHIBOKEN2_VERSION}; \
    cd "${GENERATOR}/Qt/lib"; \
    for lib in libQt5*.so.5; do \
        if [ -e "/opt/Natron-sdk/qt5/lib/$lib" ]; then \
            rm -f "$lib"; \
            ln -s "/opt/Natron-sdk/qt5/lib/$lib" .; \
        fi; \
    done; \
    printf '#!/bin/sh\nLD_LIBRARY_PATH="/opt/Natron-sdk/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec %s/shiboken2 "$@"\n' "${GENERATOR}" \
        > /opt/Natron-sdk/bin/shiboken2; \
    chmod +x /opt/Natron-sdk/bin/shiboken2; \
    /opt/Natron-sdk/bin/shiboken2 --version
