#!/bin/bash
#
# Sets up a Windows machine to build Natron and its installer: run through
#   scripts/natron-build.sh setup -p windows     (MSYS2 MINGW64 shell)
#   scripts\natron-build setup -p windows        (cmd; installs MSYS2 first if needed)
#
# Every step checks first and only does what is missing, so it can be run
# again at any time, e.g. to check a machine.
#
#  1. MSYS2 packages of tools/MINGW-packages/msys2.txt (those still in the
#     MSYS2 repositories), plus zip for the portable archive
#  2. Natron SDK packages: the prebuilt ones are downloaded as tools/MINGW-packages/get.sh
#     does; the ones built for the installed Python (PySide, Shiboken...) are
#     only checked, see below
#  3. mt.exe from the Windows SDK (embeds manifests in the OFX plug-ins), with winget
#  4. this checkout: git submodules, OpenColorIO-Configs, config.pri
#
# The packages are never upgraded (no pacman -Syu): a newer MSYS2 Python
# would no longer load the PySide and Shiboken built for the current one.
#
# Options:
#   --check    only report what is missing, change nothing

set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG_DIR="$SRC/tools/MINGW-packages"
PREFIX="mingw-w64-x86_64-"
WIN_SDK_ID="Microsoft.WindowsSDK.10.0.26100"
WIN_KITS="/c/Program Files (x86)/Windows Kits/10/bin"

CHECK=0
for arg in "$@"; do
    case "$arg" in
        --check) CHECK=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

if [ "${MSYSTEM:-}" != "MINGW64" ]; then
    echo "Run this from an MSYS2 MINGW64 shell, or from cmd with scripts\\natron-build setup -p windows." >&2
    exit 1
fi

MISSING=() # what could not be set up, reported at the end

step() {
    echo
    echo "=== $*"
}

# Packages (names or provides) not installed, from the arguments.
not_installed() {
    pacman -T "$@" 2>/dev/null || true
}

# 1. MSYS2 packages
step "MSYS2 packages"
mapfile -t wanted < <(tr -d '\r' < "$PKG_DIR/msys2.txt" | grep -v '^$')
wanted+=(zip unzip rsync)
mapfile -t todo < <(not_installed "${wanted[@]}")
install=()
for p in "${todo[@]}"; do
    # Packages removed from MSYS2 since msys2.txt was written (python2, the
    # old *-git names...) cannot be installed: skip them.
    if pacman -Sp --print-format %n "$p" >/dev/null 2>&1; then
        install+=("$p")
    else
        echo "skipped, no longer in the MSYS2 repositories: $p"
    fi
done
if [ ${#install[@]} -eq 0 ]; then
    echo "all installed"
elif [ "$CHECK" = "1" ]; then
    echo "to install: ${install[*]}"
    MISSING+=("MSYS2 packages: ${install[*]}")
else
    pacman -S --needed --noconfirm "${install[@]}" ||
        MISSING+=("MSYS2 packages: ${install[*]} (pacman failed, see above)")
fi

# 2. Natron SDK packages
step "Natron SDK packages"
# Prebuilt packages, downloaded like get.sh does.
PREBUILT="qt4 natron-setup osmesa imath openexr imagemagick libraw-gpl2 seexpr-git sox"
GET_URL="https://sourceforge.net/projects/natron/files/MINGW-packages/current"
DOWNLOADS="$PKG_DIR/tmp-current"
for name in $PREBUILT; do
    if [ -z "$(not_installed "$PREFIX$name")" ]; then
        echo "installed: $PREFIX$name"
        continue
    fi
    file="$(grep -o "$PREFIX$name-[0-9][^ ]*-any\.pkg\.tar\.[a-z]*" "$PKG_DIR/get.sh" | head -1)"
    if [ -z "$file" ]; then
        MISSING+=("$PREFIX$name: not in tools/MINGW-packages/get.sh")
    elif [ "$CHECK" = "1" ]; then
        echo "to install: $file"
        MISSING+=("$PREFIX$name")
    else
        mkdir -p "$DOWNLOADS"
        { [ -f "$DOWNLOADS/$file" ] || curl -fL --retry 3 -o "$DOWNLOADS/$file" "$GET_URL/$file/download"; } &&
            pacman -U --needed --noconfirm "$DOWNLOADS/$file" ||
            { rm -f "$DOWNLOADS/$file"; MISSING+=("$PREFIX$name: download or install of $file failed"); }
    fi
done
# Packages that must be built against the installed MSYS2 libraries and
# Python: the prebuilt ones of get.sh are for an older Python and do not load.
# They are built from their PKGBUILD in tools/MINGW-packages (makepkg-mingw).
BUILT="natron_shiboken natron_pyside natron_openimageio natron_opencolorio-git natron_ffmpeg-gpl2 natron_x264 natron_poppler"
for name in $BUILT; do
    if [ -z "$(not_installed "$PREFIX$name")" ]; then
        echo "installed: $PREFIX$name"
    else
        echo "MISSING:   $PREFIX$name (build it from its PKGBUILD in tools/MINGW-packages)"
        MISSING+=("$PREFIX$name: build it from its PKGBUILD in tools/MINGW-packages")
    fi
done

# 3. mt.exe from the Windows SDK
step "Windows SDK (mt.exe)"
find_mt() {
    command -v mt.exe 2>/dev/null ||
        ls "$WIN_KITS"/10.*/x64/mt.exe 2>/dev/null | sort -V | tail -1
}
if MT="$(find_mt)" && [ -n "$MT" ]; then
    echo "found: $MT"
elif [ "$CHECK" = "1" ]; then
    echo "to install: $WIN_SDK_ID"
    MISSING+=("mt.exe: winget install --id $WIN_SDK_ID")
else
    WINGET="$(command -v winget.exe 2>/dev/null || echo "$(cygpath -u "$LOCALAPPDATA")/Microsoft/WindowsApps/winget.exe")"
    if [ ! -x "$WINGET" ]; then
        MISSING+=("mt.exe: install the Windows SDK ($WIN_SDK_ID), winget was not found")
    else
        echo "Installing $WIN_SDK_ID (approve the administrator prompt)..."
        "$WINGET" install --id "$WIN_SDK_ID" --exact --source winget \
            --accept-package-agreements --accept-source-agreements --silent || true
        if MT="$(find_mt)" && [ -n "$MT" ]; then
            echo "found: $MT"
        else
            MISSING+=("mt.exe: the Windows SDK installation did not provide it")
        fi
    fi
fi

# 4. This checkout
step "Natron checkout: $SRC"
cd "$SRC"
if git submodule status --recursive | grep -q '^[-+]'; then
    if [ "$CHECK" = "1" ]; then
        MISSING+=("git submodules: git submodule update -i --recursive")
    else
        git submodule update -i --recursive || MISSING+=("git submodules: update failed")
    fi
else
    echo "submodules: up to date"
fi
# The OpenColorIO configs release matching this Natron version (2.5, ...).
OCIO_VERSION="$(sed -n 's/^#define NATRON_VERSION_MAJOR NATRON_VERSION_MAJOR_\([0-9]\)\([0-9]\)$/\1.\2/p' Global/Macros.h)"
if [ -d OpenColorIO-Configs ]; then
    echo "OpenColorIO-Configs: present"
elif [ -z "$OCIO_VERSION" ]; then
    MISSING+=("OpenColorIO-Configs: could not read the Natron version in Global/Macros.h")
elif [ "$CHECK" = "1" ]; then
    MISSING+=("OpenColorIO-Configs (Natron-v$OCIO_VERSION)")
else
    archive="OpenColorIO-Configs-Natron-v$OCIO_VERSION.tar.gz"
    # relative archive path: tar takes C:/... for a remote host
    if curl -fL --retry 3 -o "$archive" \
           "https://github.com/NatronGitHub/OpenColorIO-Configs/archive/Natron-v$OCIO_VERSION.tar.gz" &&
       tar xzf "$archive" &&
       mv "OpenColorIO-Configs-Natron-v$OCIO_VERSION" OpenColorIO-Configs; then
        echo "OpenColorIO-Configs: Natron-v$OCIO_VERSION"
    else
        MISSING+=("OpenColorIO-Configs: download of Natron-v$OCIO_VERSION failed")
    fi
    rm -f "$archive"
fi
if [ -f config.pri ]; then
    echo "config.pri: present"
elif [ "$CHECK" = "1" ]; then
    MISSING+=("config.pri")
else
    touch config.pri # the SDK is found through pkg-config
    echo "config.pri: created"
fi

# Summary
step "Summary"
echo "gcc:     $(gcc -dumpfullversion 2>/dev/null || echo missing)"
echo "Qt:      $(pkg-config --modversion QtCore 2>/dev/null || echo missing)"
echo "Python:  $(python3 -c 'import platform; print(platform.python_version())' 2>/dev/null || echo missing)"
for tool in qmake zip rsync; do
    command -v "$tool" >/dev/null || MISSING+=("$tool")
done
if [ ${#MISSING[@]} -eq 0 ]; then
    echo "Ready to build: scripts/natron-build.sh (or scripts\\natron-build from cmd)"
else
    echo "Not ready, missing:"
    printf '  - %s\n' "${MISSING[@]}"
    exit 1
fi
