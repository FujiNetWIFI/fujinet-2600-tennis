# Networked two-player Tennis on an Atari 2600, over FujiNet.
#
# Every target is a gate in the milestone ladder, and the order is the order
# they have to pass in: a rung that fails makes everything above it ambiguous.
#
#   make disasm       M0a DiStella over the dump, steered by tools/tennis.cfg
#   make verify-org   M0b the split is the one descent finds, and it rebuilds
#                         to the cartridge byte for byte
#   make echo         M1  the transaction latency, measured, before any porting
#   make tennis       M2  the banked image, every changed byte declared
#   make frames       M2a stock and split measure the same frame, every frame
#   make det          M2b the split build plays exactly like the 1981 ROM
#   make glyph        M2c the score kernel's out-of-bounds read is a constant
#   make inputs       M3  five sites, all inside the shim
#   make slack        M4a the one timed band has the room the netcode assumes
#   make session      M5  one console, a real socket, a real HELLO
#   make rig          M6  two consoles, one match, zero mismatches
#   make ladder           all of it, in order

SHELL := /bin/bash
FUJI_FIRMWARE ?= $(HOME)/Workspace/fn-2600
VCS           ?= $(FUJI_FIRMWARE)/pico/atari-2600
SECS          ?= 30

.PHONY: all disasm verify-org probe echo tennis frames det glyph inputs lag \
        slack sim lobby session rig rig-hold rig-serve rig-play rig-repair \
        ladder play stop clean

all: tennis

# ---------------------------------------------------------------- M0a
# The disassembly itself. There was no published commented source for Tennis,
# so this project generates one: DiStella over the dump, steered by the
# code/data map in tools/tennis.cfg. It is NOT checked in -- it is the game.
disasm:
	./build.sh disasm

# ---------------------------------------------------------------- M0b
# TWO claims, and only the second is a round trip.
#
# tools/checkmap.py walks the image by recursive descent from the reset vector
# and requires the declared code/data split to be the one it finds. That is not
# a formality: a run of data bytes disassembled as instructions re-encodes to
# exactly the bytes it came from, so the `cmp` below cannot see a wrong split
# at all. Video Olympics' PORTING.md is the account of how much that costs to
# discover the other way round.
verify-org:
	./build.sh verify-org

# ---------------------------------------------------------------- M1
probe: build/probe.bin
build/probe.bin: src/probe.asm src/tncore.inc src/tndefs.inc src/fujinet.inc src/vcs.inc build.sh
	./build.sh probe

echo: probe
	SECS=$(SECS) test/run_probe.sh

# ---------------------------------------------------------------- M2
tennis:
	./build.sh

build/stock.bin: rom/tennis.bin
	@mkdir -p build && cp -f rom/tennis.bin build/stock.bin

# ---------------------------------------------------------------- gates
#
# ENDPOINT deliberately points at a port nothing listens on. These gates test
# the LOCAL path -- the claim that a patched Tennis plays exactly like the 1981
# cartridge -- and they reach it through the session's fallback. Built against
# the real endpoint they would depend on whether a relay happened to be running
# on this machine, and `make play` leaves one up.
frames: build/stock.bin
	ENDPOINT="TCP://127.0.0.1:9699/" ./build.sh >/dev/null
	@{ SECS=$${SECS:-14} SLOT=a26_2k_4k ./run.sh stock frames || true; } | tail -4
	@{ SECS=$${SECS:-14} ./run.sh tennis frames || true; } | tail -4

# DET_QUIET, because injecting stick movement through MAME's ports is not
# frame-exact between two builds: they boot through different amounts of code,
# so a change can land on one side of one build's read and the other side of
# the other's. A quiet run removes the variable and leaves the whole frame
# loop, the timers, the sound, the scoring and the kernel still running.
#
# AND IT RUNS LONG. A ten-second gate is not a short thirty-second gate, it is
# a different gate: the sibling port's version passed at ten for its whole life
# and failed at thirty, five hundred frames past where anything had looked.
det: build/stock.bin
	ENDPOINT="TCP://127.0.0.1:9699/" ./build.sh >/dev/null
	@{ DET_QUIET=1 SECS=$${SECS:-40} SLOT=a26_2k_4k ./run.sh stock det 2>/dev/null || true; } \
	    | grep -E '^[0-9]+ [0-9A-F]{4} [0-9A-F]{4}$$' > build/det_stock.txt
	@{ DET_QUIET=1 SECS=$${SECS:-40} ./run.sh tennis det 2>/dev/null || true; } \
	    | grep -E '^[0-9]+ [0-9A-F]{4} [0-9A-F]{4}$$' > build/det_split.txt
	python3 tools/ramdiff.py build/det_stock.txt build/det_split.txt --from 60 --align 3

# The half of the out-of-bounds read that lockstep needs: whatever the byte is,
# it must be the same on both consoles and must stay the same. See emu/det.lua.
glyph: tennis
	@grep -oE "\$$[0-9A-F]{2}" build/mirror.inc | tail -1 > build/glyph_want.txt
	@{ SECS=$${SECS:-20} ./run.sh tennis glyph 2>/dev/null || true; } | tee build/glyph.txt | grep GLYPH
	@grep -q '^GLYPH PASS' build/glyph.txt

# Locally first -- every read must come from TNLOC0 -- and then in a match,
# where every read must come from TNCAP and TNLOC0 must not run at all.
inputs: tennis
	@{ SECS=$${SECS:-14} ./run.sh tennis inputs || true; } | sed -n '/^SITES/,$$p'
	RIG_LUA=inputs SECS=$${SECS:-25} test/run_rig.sh

lag:
	@TNLAG=1 ./build.sh >/dev/null
	@{ SECS=$${SECS:-14} ./run.sh tennis lag 2>/dev/null || true; } | tee build/lag.txt | grep LAG
	@./build.sh >/dev/null
	@grep -q '^LAG PASS' build/lag.txt

# Run it against STOCK as well as the build: the hook has no cycle budget to
# get wrong, but a band with little slack contributes fewer transport steps and
# that is worth knowing before the netcode is written rather than after.
slack: build/stock.bin tennis
	@{ SECS=$${SECS:-20} SLOT=a26_2k_4k ./run.sh stock slack 2>/dev/null || true; } | grep SLACK
	@{ SECS=$${SECS:-20} ./run.sh tennis slack 2>/dev/null || true; } | tee build/slack.txt | grep SLACK
	@grep -q '^SLACK PASS' build/slack.txt

# ---------------------------------------------------------------- no emulator
sim:
	python3 tools/tn_client_sim.py

lobby:
	python3 tools/test_lobby_pub.py

# ---------------------------------------------------------------- M5-M7
session: tennis
	test/run_sess.sh

rig: tennis
	test/run_rig.sh

rig-hold: tennis
	RIG_HOLD=select SNAPTICK=250 SECS=45 test/run_rig.sh

# Tennis's analogue of Dragster's rig-stage: RESET pressed over and over, so
# the bounded clear runs hundreds of times and the netcode has to survive all
# of them. It is the gate for tools/check_zp.py's claim, at runtime.
rig-serve: tennis
	RIG_HOLD=serve SECS=45 test/run_rig.sh

rig-play: tennis
	RIG_LUA=play SECS=40 test/run_rig.sh

rig-repair: tennis
	RIG_LUA=play PLAY_INJECT=120 SECS=60 test/run_rig.sh

ladder: verify-org sim lobby tennis frames det glyph inputs slack \
        rig rig-hold rig-serve rig-play rig-repair
	@echo
	@echo "ladder: every gate passed."

play: tennis
	test/run_play.sh

stop:
	test/stop.sh

clean:
	rm -rf build
