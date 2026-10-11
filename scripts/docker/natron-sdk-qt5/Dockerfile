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
# Clang's built-in headers (stddef.h, limits.h...) for the generator's
# libclang 6: the wheel does not have them, nor does the SDK's LLVM. Taken
# from Qt's prebuilt libclang 6.0.1 for RHEL 7 (only the headers are kept).
ARG LIBCLANG_URL=https://download.qt.io/development_releases/prebuilt/libclang/libclang-release_60-linux-Rhel7.2-gcc5.3-x86_64-clazy.7z
ARG LIBCLANG_DIR=/opt/Natron-sdk/libclang

# Its own layer: the archive is large (300 MB), keep it out of rebuilds.
RUN set -e; \
    /opt/Natron-sdk/bin/pip3.10 install --no-cache-dir py7zr; \
    cd /tmp; \
    curl -sSL -o libclang.7z "${LIBCLANG_URL}"; \
    /opt/Natron-sdk/bin/python3.10 -c "import py7zr, re; z = py7zr.SevenZipFile('libclang.7z'); z.extract(path='/tmp/libclang', targets=[n for n in z.getnames() if re.match(r'libclang/lib/clang/[^/]+/include/', n)])"; \
    mkdir -p "${LIBCLANG_DIR}"; \
    mv /tmp/libclang/libclang/lib "${LIBCLANG_DIR}/"; \
    rm -rf libclang.7z /tmp/libclang; \
    ls "${LIBCLANG_DIR}"/lib/clang/*/include/stddef.h

# 1. The generator wheel from Qt.
# 2. Its own Qt libraries need ICU 56, which the SDK does not have: use the
#    SDK's Qt 5 instead (as pyside2.sh does for PySide2).
# 3. The shiboken2 command on the build scripts' PATH ($SDK_HOME/bin): see
#    the shiboken2 script next to this file. Check that it runs.
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
    done

COPY shiboken2 /opt/Natron-sdk/bin/shiboken2
RUN chmod +x /opt/Natron-sdk/bin/shiboken2 && /opt/Natron-sdk/bin/shiboken2 --version

# The libraries and headers Natron compiles its bindings against: shiboken2
# and PySide2 from the wheels, through pkg-config. The build's config.pri asks
# for shiboken-py3 / pyside-py3 (the SDK's Qt 4 versions), global.pri for
# shiboken2 / pyside2: all point to the Qt 5 ones here. Linker names for the
# wheels' libraries in $SDK_HOME/lib, where the build looks for libraries.
COPY shiboken2.pc /opt/Natron-sdk/lib/pkgconfig/shiboken2.pc
COPY shiboken2.pc /opt/Natron-sdk/lib/pkgconfig/shiboken-py3.pc
COPY pyside2.pc /opt/Natron-sdk/lib/pkgconfig/pyside2.pc
COPY pyside2.pc /opt/Natron-sdk/lib/pkgconfig/pyside-py3.pc
RUN set -e; \
    SITE=/opt/Natron-sdk/lib/python3.10/site-packages; \
    cd /opt/Natron-sdk/lib; \
    ln -sf "$SITE/shiboken2/libshiboken2.abi3.so.5.15" libshiboken2.abi3.so.5.15; \
    ln -sf libshiboken2.abi3.so.5.15 libshiboken2.abi3.so; \
    ln -sf "$SITE/PySide2/libpyside2.abi3.so.5.15" libpyside2.abi3.so.5.15; \
    ln -sf libpyside2.abi3.so.5.15 libpyside2.abi3.so; \
    PKG_CONFIG_PATH=/opt/Natron-sdk/lib/pkgconfig pkg-config --cflags --libs pyside-py3
