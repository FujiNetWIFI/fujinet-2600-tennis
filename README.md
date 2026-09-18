# Networked two-player Tennis, on an Atari 2600

Activision's Tennis (1981), by David Crane, with two people playing each other
over the network through a FujiNet cartridge. Both players use joystick 1 on
their own console, and the button serves.

With no FujiNet, no relay or no opponent, it is simply Tennis. `make det`
proves it: 2243 simulated frames byte-identical to the 1981 cartridge.

```sh
make disasm       # DiStella over the dump, steered by tools/tennis.cfg
make verify-org   # the split is the one descent finds, and it IS the cartridge
make echo         # what does a mailbox transaction cost, in frames?
make tennis       # build/tennis.bin, 8192 bytes, every changed byte declared
make frames       # every frame the same length, through two bank switches
make det          # the patched build plays exactly like the 1981 ROM
make inputs       # the game no longer reads a console port at all
make slack        # the one timed band has the room the netcode assumes
make sim          # the relay protocol, with no emulator anywhere
make rig          # two consoles, one match, zero desyncs
make rig-hold     # ...with SELECT held down for the whole run
make rig-serve    # ...with RESET pressed over and over
make rig-play     # ...with both players' hands on the stick
make rig-repair   # break one console on purpose; it has to heal
make ladder       # all of it, in order
make play         # two windows, paired, playing
```

`TVSTD=ntsc` (the default) or `TVSTD=pal`. `TVSTD=secam` is refused — **not**
for Combat's reason. Tennis tells its two players apart by HUE, `$4C` and `$8C`
out of the table at `$F751`, and a SECAM TIA picks its eight colours by the hue
nibble, so it would show them perfectly well. The reason is duller: this port
has no SECAM colour table, and authoring those twelve bytes means somebody
looking at the result on a real SECAM television. The refusal lives in the
build because a 2600 cannot detect its own television standard at runtime — the
ROM generates the video timing and there is nothing to read.

**The cartridge is not in this repository.** Tennis is Activision's, 1981. This
is a patch and a server, not a place to redistribute it. Put your own
`tennis.bin` in `rom/` — see [`rom/README.md`](rom/README.md) for the md5 the
build checks against, and for why the disassembly is generated rather than
committed.

`build.sh` needs Macroassembler AS (`asl`/`p2bin`, on `PATH` or in `~/asl`),
DiStella, and the firmware tree at `$FUJI_FIRMWARE` (default `~/Workspace/fn-2600`,
on the `2600-experiment` branch). `run.sh` needs a MAME with
`pico/atari-2600/emu/apply.sh` applied, and anything touching the network needs
a `fujinet-pc`.

## How it plays

The FujiNet Lobby lists the Tennis room; picking it writes the relay's URL to
an appkey and boots this ROM. The ROM opens `N:TCP://host:9602/`, says hello,
and the relay pairs the first two consoles that turn up.

Both players use joystick 1 on their own console. **The button serves** — the
game only asks for it while a serve is pending, so a rally needs none.

The screen tells each player whether they are **player one or player two**, and
it deliberately does not tell them which end of the court they are on, because
that changes: `$D0` flips every game and the players swap ends exactly as they
do in tennis. Their scores follow them.

**Each player's own difficulty switch crosses the wire**, because it reaches
play: it clamps the range of return angles that player's racket can produce.
**Black-and-white stays local** — it reaches only the six colour cells and the
attract masks, and no colour cell reaches physics — so each player sees their
own setting.

RESET and SELECT work from either console: the two are ANDed on the wire, so
either player may press them and both machines act on the result at the same
tick.

**SELECT offers only the two-player games.** Tennis has four variations and
`$81`, which is bit 0 of the variation, decides whether the opponent is a second
human or the computer. Over a network a computer on one side is not a match —
and it is the one place in the game that invents input — so in a match SELECT
steps by two and only the odd variations are reachable. On a cartridge with no
server it steps by one and walks all four, exactly as it always did.

## How it works

**Delay-based input lockstep.** Both consoles run the whole game. A sim tick is
four video frames, so fifteen ticks a second; each console sends one packed
input byte per tick, stamped two ticks ahead, and both apply both players'
inputs at the same tick. Local input is delayed exactly as far as the peer's,
so the lag is symmetric — about 133 ms — and neither player is ahead.

`make echo` measured the ceiling first: **one frame per transaction, three
frames per WRITE/STATUS/READ cycle, 20 Hz**. Four frames to a tick ships with a
frame of margin.

**The ball is 133 ms harder to reach than it is on one console**, and nothing
can fix that. Dragster could draw its countdown eight frames early because a
countdown is a deterministic function of the simulation; where a tennis ball
goes next is a function of what the players do next, and nothing can show that
early. The delay is symmetric, which is all fairness requires, but it is real.

**A stall is free.** When the peer's input has not arrived, the frame runs
normally with the game logic skipped: the picture is regenerated from unchanged
state every frame, so it freezes coherently and the display never shudders.
Tennis never strobes `CXCLR` and never reads a collision register at all — its
collisions are coordinate arithmetic — so N stalled frames leave exactly what
one would, and two consoles that stalled for different lengths resume in
agreement.

**There is one timed band and the hook goes in front of it.** `TIM64T` is armed
at the end of the kernel with 2880 cycles and spun out at the top of the next
frame, with the whole game logic in between; `emu/slack.lua` measures 576
cycles left on the worst frame. The network machine is a chain of bounded
micro-steps run from a loop that re-reads `INTIM` before each one, so it cannot
overrun the kernel by construction — and it is patched in **ahead** of the
stock spin rather than in place of it, so every cycle it costs is spent while
the timer is still running. `PORTING.md` §2.2 is the account of the two
versions that were not.

**Tennis is fully deterministic** — no RNG, no LFSR, nothing seeded from the
timer, and `INTIM` read only by that spin. The only pseudo-randomness in the
image dithers the computer opponent, and the whitelist keeps that out of a
match entirely.

## If the two consoles ever disagree

They put themselves back together. A checksum of the simulation rides in every
record, and each console compares the peer's against its own at the one instant
they are samples of the same tick. On a difference it presses RESET — not on
its own console, but **into its own wire byte**, so the press is ANDed and
delivered through the delay ring like any real one, and both machines restart
on exactly the same tick.

The repair is total here, unlike Combat's. Tennis's positions are all in RAM and
the RESET path clears `$85-$FF` and re-runs the new-point setup, so every
checksummed cell is rebuilt. The scores go with it, and that is right — the two
consoles have just disagreed, so their scores may have too.

`make rig-repair` nudges one console's points by one mid-game and asserts they
come back: measured at **five ticks, about a third of a second**, with the
following 120 byte-identical.

## What is in here

| | |
|---|---|
| `rom/` | the cartridge dump and its generated disassembly. Neither is redistributed |
| `src/` | the 6502: the banks, the shim, the transport, the session |
| `tools/` | the disassembly gates, the bank generator, the patch map, the build audits |
| `emu/` | the MAME harnesses, one per gate |
| `test/` | the rigs: two consoles, two fujinet-pc, one relay |
| `server/` | the relay, TCP 9602 |
| `PORTING.md` | what this cost to learn |

## Not done yet

- **Real hardware.** There is no 2600 FujiNet board yet; the cartridge firmware
  says so itself. Everything here is MAME against a live `fujinet-pc`, so what
  is missing is the cartridge bus. `make echo` should be re-run there before
  anyone trusts `K = 4` and `d = 2`.
- **An appkey.** 26 is claimed here; provision it on the fujinet-firmware wiki
  registry. Combat has 24, Dragster 25.
- **The second timed window.** Tennis's overscan is an untimed 24-line `WSYNC`
  spin throwing away about 1630 cycles a frame. Arming `TIM64T` at `$F149` would
  convert it at no visual cost. Nothing needs it yet.
- **Nagle.** `NetworkProtocolTCP::open_client_connection` never calls
  `setNoDelay(true)`. That is the firmware tree's line to change, not this
  one's, and eight bytes a tick is exactly the pathological case.
