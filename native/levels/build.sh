#!/bin/sh
# Rebuild native/src/level_01.json from the level scripts.
#   levels/build.sh [--trace]   (--trace re-traces floor1-3.txt from the map first)
# The tracer needs numpy, scipy and Pillow, and the map + markers from
# Polygon's Care Center interactive map (see trace_polygon.py).
set -e
cd "$(dirname "$0")"
if [ "$1" = "--trace" ]; then
    python3 trace_polygon.py "${MAP:-Care_Center-8192.png}" "${MARKERS:-care_center_markers.json}" scripts
fi
mkdir -p ../.fibs/tests
cc -std=c11 -O1 -I../src -I../libs -o ../.fibs/tests/levelscript ../tools/levelscript.c ../src/levelscript.c ../src/level.c
../.fibs/tests/levelscript scripts/care_center.txt scripts/floor1.txt scripts/floor2.txt scripts/floor3.txt scripts/fixes.txt \
    scripts/links.txt -o ../src/level_01.json
