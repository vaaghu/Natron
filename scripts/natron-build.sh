#!/bin/bash
#
# Fast Natron builds and installers of this working tree, reusing everything
# a previous run produced.
#
# Usage: scripts/natron-build.sh [MODE] [OPTIONS]
#
# Modes:
#   all        build Natron, then the installer and portable archive (default)
#   build      build Natron only (no installer)
#   installer  make the installer and portable archive from the existing build
#   plugins    re-clone and rebuild the OFX plug-ins (otherwise built once and kept)
#   status     show what the cache holds
#   clean      delete the cache (the next run starts from scratch)
#
# Options:
#   -p PLATFORM    linux, windows or macos (default: this machine's)
#   -j N           parallel compile jobs (default: all cores)
#   -n NAME        build name: output folder <out>/<NAME>/<NUMBER> (default: the branch name)
#   -b NUMBER      build number (default: the next one for NAME: 1, 2, 3...)
#   -d             linux only: run detached; follow with: docker logs -f natron-build
#   --tests        run Natron's unit tests (skipped by default)
#   --committed    refuse to build with uncommitted changes (build = HEAD)
#   --qt4, --qt5   Qt to build against (default: 4, or 5 on Apple Silicon)
#
# Platforms: the build scripts (tools/jenkins) build for the machine they run
# on; they cannot cross-compile. Each platform is built on its own host:
#   linux    any Linux machine with docker (natron-sdk image)
#   windows  a Windows machine, from an MSYS2 MINGW64 shell with the Natron
#            Windows SDK installed (tools/MINGW-packages/README.md)
#   macos    a Mac with Xcode and the Natron SDK (tools/MacPorts or tools/homebrew)
# The modes, options and cache work the same on all three.
#
# Environment:
#   NATRON_BUILD_CACHE  cache directory (default: ../natron-build-cache next to this repo,
#                       or ../natron-build-cache-<platform> for windows and macos)
#   NATRON_BUILD_OUT    output directory (default: ../builds next to this repo)
#   NATRON_SDK_IMAGE    linux SDK image (default: natrongithub/natron-sdk:latest)
#
# What is reused between runs (all under the cache directory):
#   tmp/Natron         sources and object files: only changed files are recompiled
#   tmp/openfx-*       plug-in checkouts (cloned once)
#   tmp/tmp_deploy     built Natron and OFX plug-ins: plug-ins are built once
#   src/               downloads (OpenColorIO configs, ~280 MB, fetched once)
# The working tree is copied in with rsync: nothing needs to be pushed, and
# uncommitted changes are built too (see --committed).
#
# Steps skipped compared to a plain run of the build scripts:
#   - wiping and re-cloning everything (DEBUG_SCRIPTS=1 keeps the cache)
#   - rebuilding the plug-ins (until `plugins` is run)
#   - unit tests (SKIP_NATRON_TESTS=1, unless --tests)
#   - step 6, which trims the output directory to 8 GB by deleting old builds

set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${NATRON_BUILD_OUT:-$(dirname "$SRC")/builds}"
IMAGE="${NATRON_SDK_IMAGE:-natrongithub/natron-sdk:latest}"
CONTAINER="natron-build"

case "$(uname -s)" in
    Linux) HOST=linux ;;
    MINGW64_NT-*|MINGW32_NT-*|MSYS_NT-*|Msys) HOST=windows ;;
    Darwin) HOST=macos ;;
    *) HOST=unknown ;;
esac

MODE="all"
PLATFORM="$HOST"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
BRANCH="$(git -C "$SRC" rev-parse --abbrev-ref HEAD)"
# Build name: the branch, usable as a folder name (feature/x -> feature-x);
# on a detached HEAD, detached-<commit>.
if [ "$BRANCH" = "HEAD" ]; then
    NAME="detached-$(git -C "$SRC" rev-parse --short HEAD)"
else
    NAME="$(echo "$BRANCH" | tr '/' '-')"
fi
NUMBER="" # next free one, see next_build_number
DETACH=0
TESTS=0
COMMITTED=0
QT=""

usage() {
    sed -n '3,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
    exit "${1:-0}"
}

if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then
    MODE="$1"
    shift
fi
while [ $# -gt 0 ]; do
    case "$1" in
        -p) PLATFORM="$2"; shift ;;
        -j) JOBS="$2"; shift ;;
        -n) NAME="$2"; shift ;;
        -b) NUMBER="$2"; shift ;;
        -d) DETACH=1 ;;
        --tests) TESTS=1 ;;
        --committed) COMMITTED=1 ;;
        --qt4) QT=4 ;;
        --qt5) QT=5 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; usage 1 ;;
    esac
    shift
done

case "$PLATFORM" in
    linux|windows|macos) ;;
    *) echo "Unknown platform: $PLATFORM (linux, windows or macos)" >&2; exit 1 ;;
esac
if [ "$PLATFORM" != "$HOST" ]; then
    echo "A $PLATFORM build must run on a $PLATFORM machine (this one is $HOST):" >&2
    echo "the build scripts cannot cross-compile. See \`$0 --help\`, Platforms." >&2
    exit 1
fi
if [ "$PLATFORM" != "linux" ] && [ "$DETACH" = "1" ]; then
    echo "-d is for linux (docker) builds only." >&2
    exit 1
fi

if [ "$PLATFORM" = "linux" ]; then
    CACHE="${NATRON_BUILD_CACHE:-$(dirname "$SRC")/natron-build-cache}"
    QT="${QT:-4}" # the scripts would otherwise pick it from the branch name
else
    CACHE="${NATRON_BUILD_CACHE:-$(dirname "$SRC")/natron-build-cache-$PLATFORM}"
fi
DEPLOY="$CACHE/tmp/tmp_deploy"
case "$PLATFORM" in
    linux) NATRON_BIN="tmp_deploy/bin/Natron" ;;
    windows) NATRON_BIN="tmp_deploy/bin/Natron.exe" ;;
    macos) NATRON_BIN="tmp_deploy/Natron.app/Contents/MacOS/Natron" ;;
esac

running() {
    [ "$PLATFORM" = "linux" ] && docker ps -q -f "name=^${CONTAINER}$" | grep -q .
}

case "$MODE" in
    status)
        echo "Platform: $PLATFORM"
        echo "Cache:    $CACHE"
        echo "Output:   $OUT"
        for p in IO Misc CImg Arena GMIC; do
            if [ -d "$DEPLOY/OFX/Plugins/$p.ofx.bundle" ] || [ -d "$DEPLOY/Natron.app/Contents/Plugins/OFX/Natron/$p.ofx.bundle" ]; then
                s="built"
            else
                s="missing"
            fi
            printf '  plug-in %-6s %s\n' "$p" "$s"
        done
        if [ -f "$CACHE/tmp/$NATRON_BIN" ]; then
            echo "  Natron         built ($(date -r "$CACHE/tmp/$NATRON_BIN" '+%F %R'))"
        else
            echo "  Natron         not built"
        fi
        du -sh "$CACHE"/* 2>/dev/null || true
        if running; then
            echo "A build is running: docker logs -f $CONTAINER"
        fi
        exit 0
        ;;
    clean)
        read -r -p "Delete everything in $CACHE? [y/N] " answer
        [ "$answer" = "y" ] || exit 1
        if [ "$PLATFORM" = "linux" ]; then
            # Files are created by root in the container: delete them there.
            docker run --rm --mount type=bind,src="$CACHE",target=/cache "$IMAGE" \
                /bin/bash -c 'rm -rf /cache/tmp /cache/src /cache/logs /cache/scripts'
        else
            rm -rf "$CACHE/tmp" "$CACHE/src" "$CACHE/logs" "$CACHE/scripts"
        fi
        echo "Cache cleaned."
        exit 0
        ;;
    all) STEPS="BUILD_FROM=1 BUILD_TO=4" ;;
    build) STEPS="BUILD_FROM=1 BUILD_TO=3" ;;
    installer) STEPS="BUILD_FROM=4 BUILD_TO=4" ;;
    plugins) STEPS="BUILD_FROM=1 BUILD_TO=2" ;;
    *) echo "Unknown mode: $MODE" >&2; usage 1 ;;
esac

if running; then
    echo "A build is already running (docker logs -f $CONTAINER)." >&2
    exit 1
fi
if [ "$MODE" = "installer" ] && [ ! -f "$CACHE/tmp/$NATRON_BIN" ]; then
    echo "No Natron build in the cache: run \`$0 build\` first." >&2
    exit 1
fi
# Next build number for NAME: one more than the highest used so far, from
# the numbered folders in the output directory and the counter kept in the
# cache (so a deleted build's number is not reused).
next_build_number() {
    local counter="$CACHE/build-numbers/$NAME" last=0 n d

    [ -f "$counter" ] && last="$(cat "$counter")"
    for d in "$OUT/$NAME"/*/; do
        n="$(basename "$d")"
        case "$n" in
            ''|*[!0-9]*) ;; # not a build number (e.g. date-time names)
            *) [ "$n" -gt "$last" ] && last="$n" ;;
        esac
    done
    echo $((last + 1))
}

if [ "$COMMITTED" = "1" ] && [ -n "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]; then
    echo "Uncommitted changes (--committed): commit or stash them first." >&2
    exit 1
fi
command -v rsync >/dev/null || [ "$PLATFORM" = "linux" ] || {
    echo "rsync is required (MSYS2: pacman -S rsync; macOS: included)." >&2
    exit 1
}

mkdir -p "$CACHE/tmp" "$CACHE/src" "$CACHE/logs" "$CACHE/build-numbers" "$OUT"
if [ -z "$NUMBER" ]; then
    NUMBER="$(next_build_number)"
fi
case "$NUMBER" in
    *[!0-9]*) ;;
    *) # reserved, even if the build fails; an explicit lower -b keeps the counter
        if [ ! -f "$CACHE/build-numbers/$NAME" ] || [ "$NUMBER" -gt "$(cat "$CACHE/build-numbers/$NAME")" ]; then
            echo "$NUMBER" > "$CACHE/build-numbers/$NAME"
        fi
        ;;
esac
LOG="$CACHE/logs/$NAME-$NUMBER-$MODE.log"

echo "Platform: $PLATFORM"
echo "Mode:     $MODE ($STEPS)"
echo "Source:   $SRC ($BRANCH @ $(git -C "$SRC" rev-parse --short HEAD))"
echo "Cache:    $CACHE"
echo "Output:   $OUT/$NAME/$NUMBER"
echo "Jobs:     $JOBS"

# Prepares the cache for MODE, then runs the build scripts. Runs in the
# container on linux (paths /src and /home), directly on windows and macos.
# Arguments: source dir, workspace dir (holds tmp/ src/ builds_archive/),
# Natron binary path relative to tmp/.
prepare_and_build() {
    local src="$1" workspace="$2" natron_bin="$3"
    local tmp="$workspace/tmp"

    case "$MODE" in
    all|build)
        mkdir -p "$tmp/Natron"
        # .git with mtimes (cheap on later runs). Submodules are left to the
        # checkout step, which updates them in the cache like the CI does.
        rsync -a --delete --exclude=/modules/ "$src/.git/" "$tmp/Natron/.git/"
        # Sources: copy only files whose content changed, so make rebuilds
        # exactly those.
        rsync -rl --checksum \
            --exclude=/.git --exclude=/build/ --exclude=/builds/ \
            --exclude=/Engine/Qt5/ --exclude=/Gui/Qt5/ \
            $(awk '$1 == "path" {print "--exclude=/" $3 "/"}' "$src/.gitmodules") \
            "$src/" "$tmp/Natron/"
        # With DEBUG_SCRIPTS=1, an existing binary means "already built".
        rm -f "$tmp/$natron_bin"
        # An interrupted build can leave empty object files that make takes
        # as up to date (then the link fails): remove them.
        find "$tmp" \( -name '*.o' -o -name '*.obj' -o -name '*.a' \) -size 0 -delete
        ;;
    plugins)
        rm -rf "$tmp"/openfx-* "$tmp/tmp_deploy/OFX/Plugins" "$tmp/tmp_deploy/Natron.app/Contents/Plugins/OFX/Natron"
        ;;
    esac

    env $STEPS WORKSPACE="$workspace" "${LAUNCH[@]}"
}

export MODE STEPS
export GIT_URL=https://github.com/vaaghu/Natron.git
export GIT_URL_IS_NATRON=1
export GIT_BRANCH="$BRANCH"
export BUILD_NAME="$NAME"
export BUILD_NUMBER="$NUMBER"
export MKJOBS="$JOBS"
export UNIT_TESTS=false
export DEBUG_SCRIPTS=1
export SKIP_NATRON_TESTS="$([ "$TESTS" = "1" ] && echo 0 || echo 1)"
[ -n "$QT" ] && export QT_VERSION_MAJOR="$QT"

START=$(date +%s)
STATUS=0

if [ "$PLATFORM" = "linux" ]; then
    # In the container: the cache is /home (the scripts' WORKSPACE); files
    # created as root are handed back to the host user at the end.
    INNER="$(declare -f prepare_and_build)
set -e
trap 'chown -R $(id -u):$(id -g) /home/tmp /home/src /home/builds_archive/$NAME 2>/dev/null || true' EXIT
LAUNCH=(scl enable devtoolset-11 launchBuildMain.sh)
cd /home
prepare_and_build /src /home $NATRON_BIN"

    RUN_OPTS=(--name "$CONTAINER"
        --mount type=bind,src="$SRC",target=/src,readonly
        --mount type=bind,src="$CACHE/tmp",target=/home/tmp
        --mount type=bind,src="$CACHE/src",target=/home/src
        --mount type=bind,src="$OUT",target=/home/builds_archive)
    for v in MODE STEPS GIT_URL GIT_URL_IS_NATRON GIT_BRANCH BUILD_NAME BUILD_NUMBER MKJOBS \
             UNIT_TESTS DEBUG_SCRIPTS SKIP_NATRON_TESTS QT_VERSION_MAJOR; do
        [ -n "${!v+x}" ] && RUN_OPTS+=(--env "$v=${!v}")
    done

    docker rm -f "$CONTAINER" >/dev/null 2>&1 || true # a stopped one left by -d
    if [ "$DETACH" = "1" ]; then
        docker run -d "${RUN_OPTS[@]}" "$IMAGE" /bin/bash -c "$INNER" >/dev/null
        echo "Started. Follow with: docker logs -f $CONTAINER"
        exit 0
    fi
    docker run --rm "${RUN_OPTS[@]}" "$IMAGE" /bin/bash -c "$INNER" 2>&1 | tee "$LOG" || STATUS=$? # pipefail: docker's status
else
    # Directly on this machine. The scripts update themselves from the
    # checkout, so they run from a copy in the cache, not from tools/.
    rm -rf "$CACHE/scripts"
    cp -R "$SRC/tools/jenkins" "$CACHE/scripts"
    if [ "$PLATFORM" = "windows" ]; then
        case "${MSYSTEM:-}" in
            MINGW64) export BITS=64 ;;
            MINGW32) export BITS=32 ;;
            *) echo "Run this from an MSYS2 MINGW64 shell." >&2; exit 1 ;;
        esac
    fi
    LAUNCH=(bash launchBuildMain.sh)
    (cd "$CACHE/scripts" && prepare_and_build "$SRC" "$CACHE" "$NATRON_BIN") 2>&1 | tee "$LOG" || STATUS=$?
    # The scripts write to <cache>/builds_archive: move the result to the output directory.
    if [ -d "$CACHE/builds_archive/$NAME/$NUMBER" ]; then
        mkdir -p "$OUT/$NAME"
        rm -rf "${OUT:?}/$NAME/$NUMBER"
        mv "$CACHE/builds_archive/$NAME/$NUMBER" "$OUT/$NAME/$NUMBER"
    fi
fi

ELAPSED=$(( $(date +%s) - START ))
echo
echo "Log:      $LOG"
printf 'Duration: %dm%02ds\n' $((ELAPSED / 60)) $((ELAPSED % 60))
if [ "$STATUS" = "0" ]; then
    echo "Done:     $OUT/$NAME/$NUMBER"
else
    echo "Failed with status $STATUS"
fi
exit "$STATUS"
