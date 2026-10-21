#!/bin/sh
# Rebuild native/src/level_01.json from the level scripts.
#   levels/build.sh [--trace] [--align] [--markers]
#   --trace    re-trace floor1-3.txt from the map first
#   --align    move wall runs onto their map lines (measure.py, hollow.py)
#   --markers  re-place Polygon's pickups, fixtures and door locks
#              (items.txt, locks.txt), then pick each one-sided lock's side
#              (lock_sides.txt) with the reachability tool
# The tracer needs numpy, scipy and Pillow, and the map + markers from
# Polygon's Care Center interactive map (see trace_polygon.py).
set -e
cd "$(dirname "$0")"
MAP="${MAP:-Care_Center-8192.png}"
MARKERS="${MARKERS:-care_center_markers.json}"
LS=../.fibs/tests/levelscript
REACH=../.fibs/tests/reach
OUT=../src/level_01.json
S=scripts
BASE="$S/care_center.txt $S/floor1.txt $S/floor2.txt $S/floor3.txt $S/fixes.txt $S/align.txt $S/hollow.txt $S/links.txt"
ALL="$BASE $S/items.txt $S/locks.txt $S/lock_sides.txt $S/progression.txt"
for f in align hollow items locks lock_sides progression; do [ -f "$S/$f.txt" ] || : > "$S/$f.txt"; done
for a in "$@"; do
    case "$a" in --trace) TRACE=1 ;; --align) ALIGN=1 ;; --markers) PLACE=1 ;; esac
done
if [ -n "$TRACE" ]; then
    python3 trace_polygon.py "$MAP" "$MARKERS" scripts
fi
mkdir -p ../.fibs/tests
cc -std=c11 -O1 -I../src -I../libs -o $LS ../tools/levelscript.c ../src/levelscript.c ../src/level.c
if [ -n "$ALIGN" ]; then
    # move wall runs onto their map lines: build without the old moves,
    # measure, then build again with the new ones; then hollow the thick
    # walls the moves leave behind
    : > $S/align.txt
    : > $S/hollow.txt
    $LS $BASE -o $OUT
    python3 measure.py $OUT "$MAP" /tmp/align_rooms.csv --emit-align $S/align.txt
    $LS $BASE -o $OUT
    python3 hollow.py $OUT > $S/hollow.txt
fi
if [ -n "$PLACE" ]; then
    $LS $BASE -o /tmp/level_base.json
    python3 place_markers.py /tmp/level_base.json "$MARKERS" $S
    : > $S/lock_sides.txt
    $LS $ALL -o $OUT
    sh ../tools/build_reach.sh
    $REACH $OUT --sides $S/lock_sides.txt > /dev/null
fi
$LS $ALL -o $OUT
