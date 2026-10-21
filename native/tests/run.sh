#!/bin/sh
# Builds and runs the native gameplay checks with the host C compiler.
set -e
cd "$(dirname "$0")/.."
mkdir -p .fibs/tests
cc -std=c11 -O1 -Wall -Isrc -Ilibs -o .fibs/tests/throw_test tests/throw_test.c \
    src/level.c src/grid.c src/pathfind.c src/doors.c src/interact.c src/inventory.c src/items.c \
    src/player.c src/enemy.c src/sight.c src/throw.c src/light.c src/health.c src/crafting.c src/gear.c src/savegame.c -lm
.fibs/tests/throw_test src/level_01.json
cc -std=c11 -O1 -Wall -Isrc -Ilibs -o .fibs/tests/gear_test tests/gear_test.c \
    src/level.c src/grid.c src/pathfind.c src/doors.c src/interact.c src/inventory.c src/items.c \
    src/player.c src/enemy.c src/sight.c src/throw.c src/light.c src/health.c src/crafting.c src/gear.c src/savegame.c -lm
.fibs/tests/gear_test src/level_01.json
cc -std=c11 -O1 -Wall -Isrc -o .fibs/tests/grace_state_test tests/grace_state_test.c src/grace_state.c
.fibs/tests/grace_state_test
cc -std=c11 -O1 -Wall -Isrc -o .fibs/tests/stream_test tests/stream_test.c src/stream.c -lm
.fibs/tests/stream_test
cc -std=c11 -O1 -Wall -Isrc -Ilibs -o .fibs/tests/levelscript_test tests/levelscript_test.c src/levelscript.c src/level.c src/grid.c
.fibs/tests/levelscript_test
cc -std=c11 -O1 -Wall -Isrc -Ilibs -o .fibs/tests/levelscript tools/levelscript.c src/levelscript.c src/level.c
# Playthrough: the reachability fixpoint must reach every floor cell and pickup.
sh tools/build_reach.sh
.fibs/tests/reach src/level_01.json > .fibs/tests/reach.txt
test "$(grep -c '(100.0%)' .fibs/tests/reach.txt)" -eq 3 || { cat .fibs/tests/reach.txt; echo "reach: not 100%"; exit 1; }
test "$(sed -n '/^Pickups she never reaches/{n;p;}' .fibs/tests/reach.txt)" = "  none" || { echo "reach: unreached pickups"; exit 1; }
echo "reach: 100% on every floor"
