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
cc -std=c11 -O1 -Wall -Isrc -Ilibs -o .fibs/tests/door_flush_test tests/door_flush_test.c src/level.c
.fibs/tests/door_flush_test src/level_01.json
# Playthrough: Grace's route reaches all of floors 1 and 2; on floor 3 only
# the rooms behind the Warped Door (Leon's hatchet, jammed for her) stay
# shut, with the one Med Injector in them. She buys all four Parlor prizes,
# gets the Requiem past the F2 gate and reaches the Security Manager's Office.
sh tools/build_reach.sh
R=.fibs/tests/reach.txt
.fibs/tests/reach src/level_01.json > $R
fail() { cat $R; echo "reach: $1"; exit 1; }
grep -q '  floor 1: .*(100.0%)' $R || fail "floor 1 not 100%"
grep -q '  floor 2: .*(100.0%)' $R || fail "floor 2 not 100%"
test "$(sed -n '/^Sections with floor/,/^$/p' $R | grep -c '^  F')" -eq 4 || fail "unexpected unreached sections"
test "$(sed -n '/^Sections with floor/,/^$/p' $R | grep -vc "^  F3 \(Lounge\|Alcove\|Director's Office\|Meeting Room\) \|^Sections\|^$")" -eq 0 || fail "unreached section outside the Warped Door rooms"
# the only unreached pickup: the Med Injector behind the Warped Door
test "$(sed -n '/^Pickups she never reaches/,/^$/p' $R | grep -c '^  ')" -eq 1 || fail "unexpected unreached pickup"
grep -q '^  Med Injector  *at  46, 34 (F3)' $R || fail "the Warped Door Med Injector is reachable"
! grep -q 'Basement Safe Code' $R || fail "the attic storage's Basement Safe Code is still in the level"
grep -q 'Requiem: received past the gate' $R || fail "no Requiem past the gate"
grep -q 'Parlor cabinets bought: 4 of 4' $R || fail "Parlor cabinets"
grep -q 'Antique Coins in the level: 23 ' $R || fail "not 23 Antique Coins"
grep -q '^  Rehabilitation Ward Door at 132,223 (F1): jammed' $R || fail "the Rehabilitation Ward door is not bolted shut"
grep -q 'read Examination Room Safe Code' $R && grep -q 'open Examination Room Safe' $R || fail "Examination Room Safe not opened by its file"
test "$(grep -c '"bolt": 1' src/level_01.json)" -eq 3 || fail "not 3 bolted one-sided doors"
grep -q "^Way out: Security Managers Office at 178,179 reached" $R || fail "the Security Manager's Office is not reached"
echo "reach: floors 1-2 100%, F3 Warped Door rooms shut (Leon's), Requiem past the gate, way out reached"
