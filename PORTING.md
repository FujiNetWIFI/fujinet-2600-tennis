# Porting Tennis to lockstep

What this cost to learn, written down so the next one costs less. The sibling
documents are `fujinet-2600-combat/PORTING.md`, which is the doctrine,
`fujinet-2600-dragster/PORTING.md`, and
`fujinet-2600-video-olympics/PORTING.md`. All three are assumed: everything
they say about the mailbox, the arm-then-commit rule, the sequence number
coming from the cartridge and not from RAM, the immutability of a sent record
and the shape of the gates is still true and is not repeated here.

Section numbers in the form §4.x refer to Combat's document; §3.x to Video
Olympics'.

What follows is what was different, and what was wrong.

---

## 1. The model

- **The sim clock is a tick of K video frames, and it is not the frame.**
- **Input** is captured at tick `T`, sent tagged for `T+d`, and applied on both
  consoles at `T+d`.
- **A stall** is a frame that runs normally with the game logic skipped.
- **Desync** is detected by a checksum riding every input record and repaired by
  a synthetic RESET, which restarts the match.

`K = 4`, `d = 2`: eight frames, about 133 ms. `make echo` measured the ceiling
at 20 Hz before any of it was written — 489 rounds, zero errors, one frame per
transaction — so `K = 4` ships with a frame of margin.

**There is no display lead, and that is a decision rather than an omission.**
Dragster needed one because its identity is an elapsed time to two decimal
places, and a countdown is a deterministic function of the simulation, so it
can honestly be drawn before the simulation reaches it. Tennis has no quantity
of that kind: where the ball goes next is a function of what the players do
next. Both players are delayed identically and neither is ahead; what it costs
is that the ball is about 133 ms harder to reach than on one console, and the
README says so rather than implying otherwise.

## 2. What is different about this game

### 2.1 The frame is the good shape, and it is the opposite of Combat's

Combat has no overscan and only a vertical-blank spin to live in. Tennis has
exactly one timed band, and it is the Battleship shape the family wanted all
along:

```
$F1A2   STY TIM64T      Y = $2D -- 2880 cycles, armed at the END of the kernel
  ...   the RESET/SELECT tests, the whole game logic, the display setup ...
$F051   LDA INTIM       the spin, at the TOP of the next frame
        BNE $F051
```

Measured by `emu/slack.lua` under a real match: **576 cycles left on the worst
frame and 1088 on the mean**, against Combat's 482 worst case, which shipped.
Nothing in the port ever exhausted it.

There is a second window available and unused: Tennis's overscan is not timed
at all, just a hard-coded 24-line `WSYNC` spin at `$F15F` throwing away about
1630 cycles, and arming `TIM64T` at `$F149` would convert it at no visual cost.
It is held in reserve.

### 2.2 The hook goes in FRONT of the spin, not instead of it

This is the one structural thing to carry to the next port, and it cost three
builds to reach.

The obvious patch replaces the five bytes of `LDA INTIM / BNE`. Whatever
replaces them has to get back to `$F056` somehow, and **the cycles that costs
are spent after the timer has expired**, immediately in front of `STA WSYNC`.
Almost always free — the store waits out the line anyway — and occasionally
enough to push it past the end of the line, giving a 263-line frame among 262s.

| version | cost after the timer | symptom |
|---|---|---|
| `JSR TNWAIT / NOP / NOP` | 10 cycles | a bad frame every few seconds |
| `JMP TNWAIT`, exit `JMP $F056` | 3 cycles | a bad frame every four seconds |
| **hook at `$F04C`, spin untouched** | **0** | none in 1795 frames |

The third version patches `LDA LF73F,X / STA $B2` — the five bytes immediately
before the spin — and does those two instructions itself. Everything the hook
does then happens while the timer is still running, and the spin absorbs all of
it **by construction**, which is the same reason the whole shim lives in a
timed band at all.

*The rule:* **when a hook has to sit next to a timed wait, put it on the
running side of the wait, not the expired side.**

### 2.3 A stall is free, by Dragster's argument rather than Combat's

Tennis never strobes `CXCLR` and **never reads a collision register at all** —
no `CX*` name appears in the disassembly. Its collisions are coordinate
arithmetic: `LDA $98,X / SEC / SBC $8F / CMP #$08` and its horizontal sibling.
The display setup at `$F015` rebuilds every TIA register the picture needs from
RAM on every frame. N frozen frames leave exactly what one would.

The same fact removes a landmark every sibling harness relies on. `emu/det.lua`,
`emu/rig.lua` and `emu/play.lua` all sample at `STA CXCLR` in the other ports;
here they sample at `$F1A2`'s `STY TIM64T`, which is written exactly once per
frame and lives at `$0296` in the RIOT — where the RAM clear, which sweeps the
whole TIA register file, cannot reach it. §2.7 is what happens when that is
not noticed.

### 2.4 Two index spaces, and the one that moves during play

The per-player loop counts a **court index** in `$89`, 1 then 0, and it indexes
everything positional. Identity — difficulty, trigger, points, games — is
indexed by the **port index**, `$89 EOR $D0`.

**`$D0` flips every game.** That is how the players change ends while their
scores follow them, exactly as they do in tennis. So the role is in port space
and must never be keyed off the court index; the shim mixes in port space and
lets the game's own `EOR` do the rest. `$D0` is in the checksum, because two
consoles that disagree about it have each player standing at the wrong end
while agreeing about every coordinate.

It is Video Olympics §3.5 — "`$92`/`$93` are not the role mapping" — in a form
that also moves during play. The screen therefore names the PLAYER and cannot
name the end.

### 2.5 The out-of-bounds read the 1981 cartridge depends on

`$F527`'s `LDA ($B2),Y` runs once per frame with Y having reached `$FF`, so it
reads `($B3:$B2) + $FF`. `$B3` is `$F7` and **`$B2` is the literal `$F6`** —
`$F14D` is `SEC / LDA #$F6 / LDX #$06` and the loop at `$F154` writes it eight
bytes before the call, every frame, whatever the score. So the address is
`$F8F5`, past the end of a 2K cartridge.

In the stock image the 2K mirrors into the 4K slot and it folds to `$F0F5`, a
byte of the display kernel. **In the FujiNet cartridge `$1800-$1FFF` is the
mailbox**, so it lands on a text plane instead.

It is not decoration. The value lands in `$8A`, and `$F396` and `$F3AB` use
`$8A` to clamp the range of return angles a racket can produce:

| what the planes held | `$8A` | effect | `make det` |
|---|---|---|---|
| undriven | `$FF` | the clamp never fires | 2384 frames identical |
| blanked by `CDBLANK` | `$00` | the clamp fires on every return | diverges at frame 43 |
| **the fold, reproduced** | **`$53`** | **stock's** | **2243 frames identical** |

So it is reproduced rather than excused: `tools/mkmirror.py` reads the byte out
of the dump and `TNFCNT` writes `$8A` from it once a frame.

**And WHERE it is written decided whether the gate was honest.** Either the
stall gate at `$F1A5` or the counter hook at `$F170` is before the game logic
reads `$8A`, so either makes the GAME right. Only `$F170` is before the timer
arm at `$F1A2`, which is where `det` and `glyph` sample — so only `$F170` makes
the OBSERVER right too. Video Olympics' "reading a cell at the wrong moment is
worse than not reading it", from the other side: *writing* one at the wrong
moment gives you a gate that reports a difference the game does not have.

### 2.6 The RAM clear has THREE ways in

```
LF004: LDY #$00
LF006: STY VSYNC,X      ; $00,X -- wraps inside page zero
       TXS
       INX
       BNE LF006
```

cold with `X = $00`, RESET with `X = $85` (`$F1A9`), SELECT with `X = $88`
(`$F1D0`). Dragster's trap with an extra door, and the last two happen several
times a minute while somebody is choosing a game — so unbounded it is not a
rare hazard, it is the normal case.

Bounded at `$D6` now. Two details worth keeping:

- **The stack pointer stops being a side effect.** Stock gets `SP = $FF` from a
  `TXS` inside the loop, which works only because the loop ends with `X = $FF`.
  A bounded loop does not, so the `TXS` comes out and the value is stated.
- **The cold entry cannot carry its seed in X**, because `TNGOTO` leaves `$FF`
  and `$FF` would clear exactly one byte and wrap out. It recognises itself by
  `TNWARM` instead, which is also what the four-byte bank dispatcher tests —
  `$1004` is `LF004` and `bit TNWARM / bmi` is exactly the four bytes available
  in front of it.

### 2.7 The variation whitelist is a parity, not a table

`$80` runs 0-3 and `$81` is `$80 AND 1`. `$81` is the two-humans flag: `$F210`'s
`LDY $81 / BNE` is what skips the sixty-nine bytes at `$F212` that **synthesise
a joystick nibble** for the computer opponent. Bit 1 of `$80` is something else
entirely — half speed, the logic chain running only on even frames.

Over a network a computer on one side is not a match, and it is the one place
in the game that invents input. So in a match SELECT **steps by two** and the
wrap goes to 1 rather than 0; locally it steps by one and walks all four, which
is what keeps `make det` a comparison against the 1981 cartridge. The cold path
forces an odd variation, because the clear leaves `$80` at zero and zero is the
computer.

`make rig-hold` is the proof: SELECT held for 45 seconds walks
`3 1 3 1 3 1 3 1 3` at ticks 33, 41, 50, 58, 67, 75, 84, 92, 101 — **on both
consoles, to the tick**.

---

## 3. The expensive lessons

### 3.1 `$FF` is not a NOP, and filler after a JSR is EXECUTED

The `$F170` patch is three bytes over eleven. The first version was
`JSR TNFCNT` and eight bytes of `$FF` padding — and a JSR returns to the byte
after it, so the CPU ran the padding.

`$FF` is the undocumented `ISC abs,X`, a read-modify-write. `ISC $FFFF,X` with
the `X = $03` that is live there addresses **`$0002`**. WSYNC, strobed three
times per instruction, twice per frame.

The build measured **268 scanlines** instead of 262 and **painted the screen
black**, because `$83` — the attract colour mask — was one of the cells the
runaway wrote on its way through. A `JMP` over the padding is the whole fix.

*The rule:* padding is only inert if nothing reaches it. After a `JSR`,
something does.

### 3.2 A gate that passes on nothing

`emu/rig.lua` taps `STA CXCLR` in every sibling port. Tennis never strobes it,
so the tap fired only when the RAM clear swept the TIA — once per RESET, not
once per frame. The snapshot was therefore never taken, and `SNAP` printed
`never reached the snapshot tick` on **both** consoles.

The verdict compared those two strings, found them equal, and said

```
  ok   the two consoles agree byte for byte at the snapshot tick
```

Not a gate that failed. A gate that passed on nothing — §4.22 in a fresh
disguise, and it is the reason the rig now prints the snapshot it compared.

### 3.3 The watchdog counted frames and called them ticks

A stall retries the SAME boundary on the next frame: the phase is deliberately
not bumped, so the sim resumes the instant the peer's record lands rather than
up to K frames later. Which means the stall path runs **once per frame**, and
the sibling's comment — "fifteen in a row at four frames each is a second" —
described a quarter of a second.

A quarter of a second is inside the ordinary jitter of two emulators, two
`fujinet-pc` instances and a relay sharing one machine. The two consoles gave
up on each other about a second after handover, went back to the boot bank,
reconnected, and did it again.

**What it looked like is why it is worth writing down.** A match that restarts
every three seconds, with **zero CRC mismatches in the relay log** — because
everything that ran, ran in perfect lockstep. The relay cheerfully reported
"64 CRC rounds verified" on each of them. Every agreement gate was green.

The counter is divided by `K` now and the high nibble really does count ticks.

### 3.4 STATUS sent unit 0, and the right answer was eight lines away

The family's note reads "STATUS is `$53` with two 1-byte params (1,0,1,0)".
Written raw that is the four bytes `01 00 01 00` — which is two parameters
whose values are **both zero**, because the 1s in that phrase are the SIZE
bytes `FNPB` supplies. NET STATUS refuses unit 0.

The transport was byte-identical to Dragster's, which is what made it hard to
suspect. What made it easy in the end was that the SESSION's own STATUS had
been right all along, eight lines of `jsr FNPB` away in the same repository —
and `dlen:4` sitting next to `dlen:-1` in one `fujinet-pc` log is what made the
two comparable.

*The rule:* when two pieces of your own code do the same thing and one works,
diff them before reading anything else.

### 3.5 The role was parked in a cell the text kernel owns

`tndisp.inc` says `CDPAD3 EQU TNTMP`. The 48-pixel text kernel borrows it as a
padding counter, because the boot bank runs before a match and the lockstep
cells are not live yet.

The role arrives in the START frame and is wanted at the handover — a hundred
and fifty frames of "PLAYING" and the opponent's name later. So the role was
whatever the kernel had last counted to, and **both consoles came up as the
guest**: both swapping the two wire bytes, both therefore agreeing perfectly,
and each player moving the far player instead of their own.

Video Olympics §3.17 is the same lesson with a RAM clear doing the sweeping
instead of a display kernel. The fix is the same: park it somewhere nobody else
owns. `TNENT` costs the same two stores.

### 3.6 `ADC zp,Y` does not exist

The checksum walks a table of cell addresses. The obvious spelling is
`LDY TNCRCT,x` then `ADC $00,y`, and the 6502 has `LDX abs,Y` and `ADC zp,X`
and neither of their mirrors. The table is indexed with Y and the cell with X.

An assembler that had quietly accepted it would have been the more interesting
problem.

### 3.7 What `det` can honestly claim when the input moves

The shim samples the console once a frame and the game reads the shadows on its
next pass, so every input is uniformly one frame older than stock's. That is
invisible while nothing changes and becomes a permanent OFFSET the moment
something does: RESET restarts the game, so the build restarts one frame after
stock and every state that follows is stock's, one frame later.

It is not a difference in what the simulation computes, and both consoles in a
match sample at the same point, so it is not a lockstep concern at all. But a
checksum cannot express "the same, one frame later".

So `tools/ramdiff.py` finds the offset, NAMES it, requires every remaining frame
to be identical, **and** requires the game's own 16-bit frame counter — carried
in its own column rather than folded into the checksum — to differ by exactly
that offset and by nothing else. A build that is genuinely one frame behind
satisfies both; one that had drifted would fail the second while passing the
first.

`$84` and `$88` go in that column together, because they are one counter:
`INC $84 / BNE / INC $88`, and the two builds wrap one frame apart.

### 3.8 Choosing what to corrupt is choosing what the test proves

`make rig-repair` breaks one console on purpose. A player's position is the
obvious pick and it is useless: `$98-$9B` are driven from the stick every tick,
so the corruption is gone by the next one without anything having repaired it —
a test that passes itself.

`$C5` is the host's points in the current game. It persists, it is in the
checksum, and nothing rewrites it but the scoring routine. Measured: **five
divergent ticks in one episode, repaired in 0.3 seconds, the following 120
ticks byte-identical.**

---

## 4. The bank split, and why it was cheap

Three cross-bank references exist in the whole of Tennis:

```
$F494  JMP LF015     GAME -> KERN.  SEAM A.
$F302  LDA LF700,X   GAME -> a table now carried in both banks
$F30B  LDA LF704,X   GAME -> the same
```

**Both seams are fall-throughs and cost no patched bytes**, because every byte
keeps its cartridge address and the bank that does not own a seam has a hole
exactly there. Seam A serves two callers with one trampoline: `JMP LF015` every
frame, and the cold path falling out of `JSR LF5E7`.

**Seam B is at `$F168` and not `$F16C` on purpose.** `LDX #$03` feeds the three
`STX WSYNC/VSYNC/VBLANK` stores after it and `TNGOTO` clobbers X, so putting
the load on the game side means nothing has to survive the switch. Y *does*
survive and must — the tail spin leaves it at 0 and `$F17B`'s `DEY` turns that
into `$FF` — and `TNGOTO` never touches Y. The switch costs about 35 cycles and
they are free: the next instruction is `STA WSYNC`, the spin exits 7 cycles
into a scanline, and the store still waits out the same line.

**`$F7FE/$F7FF` are not vectors.** `AND LF7FE,X` at `$F1FC` reads them as the
two difficulty-switch masks, `$40` and `$80`; David Crane reclaimed the IRQ
vector as data. In the banked layout the real vectors live in the fixed tail,
so the region map declares those two bytes as data in the bank that reads them.
Any tool that treats them as vectors and rewrites them silently breaks both
difficulty switches.

## 5. The disassembly, and the claim the round trip cannot make

No commented disassembly of Tennis exists, so this port generates one with
DiStella and proves it twice. Only the second is a round trip:

1. **`tools/checkmap.py`** walks the image by recursive descent from the reset
   vector and requires the declared code/data split to be the one it finds.
2. and the result reassembles to `rom/tennis.bin` byte for byte.

The first is the load-bearing one: **a run of data bytes disassembled as
instructions re-encodes to exactly the bytes it came from**, so the `cmp` cannot
see a wrong split at all. Video Olympics had to hand-steer `$F337-$F3FF` because
an absolute-indirect `JMP` ends a trace and everything past it looks like data.

Tennis is unusually well behaved and the checker says so rather than assuming
it: 857 instructions, 1609 code bytes, the remaining 439 in one contiguous run
at `$F649-$F7FF`, **no `JMP (ind)` and no RTS-dispatch anywhere** — all four
`PHA` sites matched by a `PLA` in the same routine. So the descent is exact
rather than a lower bound, and both facts are asserted on every build, because
the day one of them stops being true is the day the gate would start lying.

## 6. Status

| gate | result |
|---|---|
| `verify-org` | 857 instructions, 1609 code bytes; the declared split is descent's; 2048 bytes identical to the dump |
| `echo` | **489 rounds, 0 errors, 1 frame per transaction, 3 per WRITE/STATUS/READ = 20 Hz** |
| `tennis` | 8192 bytes; checkrom, check_zp clean; 2109 bytes compared, 59 changed, **all declared** |
| `frames` | 1795 frames, every one 262 lines, through two bank switches a frame; the only odd ones are the boot bank handing over |
| `det` | **2243 simulated frames identical to the 1981 cartridge**, 2170 distinct states |
| `glyph` | the out-of-bounds read is the constant stock folds to, across 800 frames |
| `inputs` | 4 sites, all inside `TNLOC0`; the game's own five are gone |
| `slack` | 576 cycles on the worst frame, 1088 mean, 0 exhausted |
| `sim` | 22 protocol conformance checks, no emulator |
| `lobby` | 11 registration checks against a mock |
| `session` | socket opened, HELLO delivered, the relay names the player from the username appkey |
| `rig` | **313 and 316 ticks, 0 CRC mismatches**, byte-identical simulation at tick 150 |
| `rig-hold` | SELECT held 45 s: 612 and 615 ticks, the variation walks `3 1 3 1 3 1 3 1 3` identically on both |
| `rig-serve` | RESET pressed ~35 times in 45 s: 600 and 602 ticks, 0 mismatches — the bounded clear holds under play |
| `rig-play` | two consoles with their hands on the sticks, **539 ticks in common, zero divergence at any of them** |
| `rig-repair` | `$C5` nudged at tick 120: detected, **repaired in 5 ticks**, the following 120 byte-identical |

Never run on hardware. Every number here is MAME plus a real `fujinet-pc`
speaking BoIP to a real socket; what is missing is the cartridge bus.
