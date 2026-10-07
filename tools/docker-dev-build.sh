#!/bin/bash
#
# Incremental Linux build of this working tree inside the natron-sdk docker image.
#
# Everything the build produces is kept in $NATRON_BUILD_CACHE on the host, so
# later runs reuse it:
#   tmp/Natron           copy of this repo (object files stay next to the sources)
#   tmp/openfx-*         plug-in checkouts
#   tmp/tmp_deploy       built Natron binaries and OFX plug-ins
#   builds_archive/      portable Natron archive (the thing to run)
#
# The first run clones and builds the plug-ins (slow). Later runs skip the
# plug-ins, sync only changed sources, and rebuild Natron incrementally.
# Uncommitted changes are built too; nothing needs to be pushed.
#
# Environment:
#   NATRON_BUILD_CACHE  cache directory (default: ../natron-build-cache next to this repo)
#   MKJOBS              parallel jobs (default: 2)
#   BUILD_GMIC=0        do not build the G'MIC plug-ins (also BUILD_IO, BUILD_MISC, BUILD_ARENA)
#
#   BUILD_TO            last step: 3 = stop after compiling Natron, 4 = also make the
#                       portable archive (default: 4)
#
# QT_VERSION_MAJOR  Qt to build against: 4 (default) or 5. Set explicitly because
#                   the build scripts pick it from the branch name.
#
# To start from scratch: sudo rm -rf "$NATRON_BUILD_CACHE" (files are owned by root).

set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${NATRON_BUILD_CACHE:-$(dirname "$SRC")/natron-build-cache}"
IMAGE="${NATRON_SDK_IMAGE:-natrongithub/natron-sdk:latest}"

mkdir -p "$CACHE/tmp" "$CACHE/builds_archive"

echo "Source: $SRC"
echo "Cache:  $CACHE"

docker run --rm -it \
    --mount type=bind,src="$SRC",target=/src,readonly \
    --mount type=bind,src="$CACHE/tmp",target=/home/tmp \
    --mount type=bind,src="$CACHE/builds_archive",target=/home/builds_archive \
    --env GIT_URL=https://github.com/vaaghu/Natron.git \
    --env GIT_URL_IS_NATRON=1 \
    --env GIT_BRANCH="$(git -C "$SRC" rev-parse --abbrev-ref HEAD)" \
    --env QT_VERSION_MAJOR="${QT_VERSION_MAJOR:-4}" \
    --env UNIT_TESTS=false \
    --env SKIP_NATRON_TESTS=1 \
    --env DEBUG_SCRIPTS=1 \
    --env BUILD_TO="${BUILD_TO:-4}" \
    --env MKJOBS="${MKJOBS:-2}" \
    --env BUILD_IO="${BUILD_IO:-1}" \
    --env BUILD_MISC="${BUILD_MISC:-1}" \
    --env BUILD_ARENA="${BUILD_ARENA:-1}" \
    --env BUILD_GMIC="${BUILD_GMIC:-1}" \
    "$IMAGE" /bin/bash -c '
        set -e
        mkdir -p /home/tmp/Natron

        # Sync the repo into the cache. With DEBUG_SCRIPTS=1 the build scripts
        # reuse an existing checkout instead of cloning.
        # .git: copied with mtimes (cheap quick-check on later runs).
        # Submodules (.git/modules and their directories) are left to the
        # checkout step, which updates them in the cache like the CI does;
        # syncing them too would flip them back and forth and force a full
        # rebuild every run.
        rsync -a --delete --exclude=/modules/ /src/.git/ /home/tmp/Natron/.git/
        # Sources: copy only files whose content changed, stamped with the
        # current time so make rebuilds exactly those.
        rsync -rl --checksum \
            --exclude=/.git --exclude=/build/ --exclude=/builds/ \
            --exclude=/Engine/Qt5/ --exclude=/Gui/Qt5/ \
            $(awk "\$1 == \"path\" {print \"--exclude=/\" \$3 \"/\"}" /src/.gitmodules) \
            /src/ /home/tmp/Natron/

        # With DEBUG_SCRIPTS=1, build-natron.sh skips compiling when a Natron
        # binary already exists; remove it so Natron is always rebuilt.
        rm -f /home/tmp/tmp_deploy/bin/Natron

        cd /home
        exec scl enable devtoolset-11 launchBuildMain.sh
    '
