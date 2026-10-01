#!/bin/sh
APPDIR=/mnt/SDCARD/App/RomM
cd "$APPDIR"

# Onion exports:
#   LD_LIBRARY_PATH=/lib:/config/lib:/mnt/SDCARD/miyoo/lib:/mnt/SDCARD/.tmp_update/lib:...
# SDL/SDL_ttf/SDL_image resolve from miyoo/lib and libcurl from .tmp_update/lib,
# but libjson-c.so.5 is on none of those paths — the only other copy on the card
# is inside App/pico/lib. Ship our own and search this dir first.
export LD_LIBRARY_PATH="$APPDIR/lib:$LD_LIBRARY_PATH"

if pgrep romm > /dev/null; then
    killall -9 romm
fi

# Keep the last run's output on the card. There is no console when Onion
# launches an app, so without this a failure to start leaves nothing to go on.
# Read it by putting the card in a reader: App/RomM/romm.log
LOG="$APPDIR/romm.log"
{
    echo "=== $(date) ==="
    echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    "$APPDIR/romm"
    echo "--- exited with status $? ---"
} > "$LOG" 2>&1
