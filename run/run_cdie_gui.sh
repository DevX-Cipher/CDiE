#!/bin/sh
# Convenience launcher for the GUI.
#
#   ./run_cdie_gui.sh [file to open]
#
# Set CDIE_DB_ROOT to the directory that holds db, db_extra and db_custom.
# Set CDIE_BUILD to the build directory if it is not the default.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

if [ -z "${CDIE_DB_ROOT:-}" ]; then
    if [ -d "$ROOT/dep/Detect-It-Easy/db" ]; then
        CDIE_DB_ROOT="$ROOT/dep/Detect-It-Easy"
    else
        CDIE_DB_ROOT="$ROOT/../_mylibs/Detect-It-Easy"
    fi
fi

CDIE_GUI_EXE="${CDIE_GUI_EXE:-$ROOT/build_linux/cdie_gui}"

if [ ! -x "$CDIE_GUI_EXE" ]; then
    echo "cdie_gui not found at $CDIE_GUI_EXE"
    echo "Build it first:  run/build_linux_gui.sh"
    exit 1
fi

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "No display found (DISPLAY / WAYLAND_DISPLAY unset)."
    exit 1
fi

export CDIE_DB_ROOT
exec "$CDIE_GUI_EXE" "$@"