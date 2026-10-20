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
[ -f scripts/align.txt ] || : > scripts/align.txt
[ -f scripts/hollow.txt ] || : > scripts/hollow.txt
cc -std=c11 -O1 -I../src -I../libs -o ../.fibs/tests/levelscript ../tools/levelscript.c ../src/levelscript.c ../src/level.c
../.fibs/tests/levelscript scripts/care_center.txt scripts/floor1.txt scripts/floor2.txt scripts/floor3.txt scripts/fixes.txt \
    scripts/align.txt scripts/hollow.txt scripts/links.txt -o ../src/level_01.json
if [ "$1" = "--align" ] || [ "$2" = "--align" ]; then
    # move wall runs onto their map lines: build without the old moves,
    # measure, then build again with the new ones
    : > scripts/align.txt
    : > scripts/hollow.txt
    ../.fibs/tests/levelscript scripts/care_center.txt scripts/floor1.txt scripts/floor2.txt scripts/floor3.txt \
        scripts/fixes.txt scripts/align.txt scripts/hollow.txt scripts/links.txt -o ../src/level_01.json
    python3 measure.py ../src/level_01.json "${MAP:-Care_Center-8192.png}" /tmp/align_rooms.csv --emit-align scripts/align.txt
    ../.fibs/tests/levelscript scripts/care_center.txt scripts/floor1.txt scripts/floor2.txt scripts/floor3.txt \
        scripts/fixes.txt scripts/align.txt scripts/hollow.txt scripts/links.txt -o ../src/level_01.json
    # then hollow the thick walls the moves leave behind
    python3 hollow.py ../src/level_01.json > scripts/hollow.txt
    ../.fibs/tests/levelscript scripts/care_center.txt scripts/floor1.txt scripts/floor2.txt scripts/floor3.txt \
        scripts/fixes.txt scripts/align.txt scripts/hollow.txt scripts/links.txt -o ../src/level_01.json
fi
