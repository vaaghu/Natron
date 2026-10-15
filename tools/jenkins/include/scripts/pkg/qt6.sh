#!/bin/bash

# Install Qt 6 (only the modules Natron and PySide6 use: qtbase, qtsvg,
# qtimageformats) in $QT6PREFIX, next to Qt 4 and Qt 5.
# see https://doc.qt.io/qt-6/linux-building.html
# The xcb platform plug-in needs the xcb-util libraries (xcb-util*-devel,
# libxkbcommon-x11-devel, and xcb-util-cursor-devel from EPEL on CentOS 7):
# see scripts/docker/natron-sdk-qt6/Dockerfile.
QT6_VERSION=6.8.4
QT6_VERSION_SHORT=${QT6_VERSION%.*}
QT6_SITE="https://download.qt.io/archive/qt/${QT6_VERSION_SHORT}/${QT6_VERSION}/submodules"
QT6_MODULES=(qtbase qtsvg qtimageformats)

if download_step; then
    for m in "${QT6_MODULES[@]}"; do
        download "$QT6_SITE" "${m}-everywhere-opensource-src-${QT6_VERSION}.tar.xz"
    done
fi
if build_step && { force_build || { [ "${REBUILD_QT6:-}" = "1" ]; }; }; then
    rm -rf "$QT6PREFIX"
fi
if build_step && { force_build || { [ ! -x "$QT6PREFIX/bin/qmake" ] || [ "$("$QT6PREFIX/bin/qmake" -query QT_VERSION)" != "$QT6_VERSION" ] || [ ! -d "$QT6PREFIX/plugins/imageformats" ]; }; }; then
    start_build
    # The SDK's pkg-config only searches the SDK: Qt also needs the system's
    # X11 libraries (xkbcommon-x11, xcb-util...), after the SDK's.
    QT6_SAVED_PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}"
    export PKG_CONFIG_PATH="${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}/usr/lib64/pkgconfig:/usr/share/pkgconfig"
    for m in "${QT6_MODULES[@]}"; do
        untar "$SRC_PATH/${m}-everywhere-opensource-src-${QT6_VERSION}.tar.xz"
        pushd "${m}-everywhere-src-${QT6_VERSION}"
        mkdir build
        pushd build
        if [ "$m" = qtbase ]; then
            # Qt's own copies of what the SDK does not have (xcb-xinput,
            # double-conversion, md4c); the SDK's for the rest.
            # Qt's COLRv1 code uses FT_LOAD_NO_SVG (FreeType 2.13.1) from
            # FreeType 2.13.0 on, the SDK's version, which ignores it: defined
            # with its 2.13.1 value (1L << 24).
            QT6_FT_FLAGS="-DFT_LOAD_NO_SVG=0x1000000L"
            cmake .. -G Ninja \
                -DCMAKE_INSTALL_PREFIX="$QT6PREFIX" \
                -DCMAKE_PREFIX_PATH="$SDK_HOME" \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_C_FLAGS="$BF $QT6_FT_FLAGS" -DCMAKE_CXX_FLAGS="$BF $QT6_FT_FLAGS" \
                -DBUILD_SHARED_LIBS=ON \
                -DQT_BUILD_EXAMPLES=OFF \
                -DQT_BUILD_TESTS=OFF \
                -DFEATURE_openssl_linked=ON \
                -DOPENSSL_ROOT_DIR="$SDK_HOME" \
                -DFEATURE_dbus_linked=ON \
                -DFEATURE_system_harfbuzz=ON \
                -DFEATURE_system_freetype=ON \
                -DFEATURE_system_pcre2=ON \
                -DFEATURE_system_png=ON \
                -DFEATURE_system_jpeg=ON \
                -DFEATURE_system_zlib=ON \
                -DFEATURE_fontconfig=ON \
                -DFEATURE_icu=ON \
                -DFEATURE_xcb=ON \
                -DFEATURE_xcb_xlib=ON \
                -DFEATURE_bundled_xcb_xinput=ON \
                -DFEATURE_opengl_desktop=ON \
                -DFEATURE_sql=OFF \
                -DFEATURE_printsupport=ON \
                -DFEATURE_journald=OFF \
                -DFEATURE_syslog=ON
        else
            "$QT6PREFIX/bin/qt-configure-module" .. -- -DCMAKE_C_FLAGS="$BF" -DCMAKE_CXX_FLAGS="$BF"
        fi
        cmake --build . --parallel "${MKJOBS}"
        cmake --install .
        popd
        popd
        rm -rf "${m}-everywhere-src-${QT6_VERSION}"
    done
    export PKG_CONFIG_PATH="$QT6_SAVED_PKG_CONFIG_PATH"
    end_build
fi
