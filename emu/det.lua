-- det.lua -- a deterministic input stream and a per-frame state checksum.
--
-- Two jobs, and they are the same job. It proves the banked build plays
-- EXACTLY like stock (M2), and it is the determinism rig two consoles have to
-- pass before lockstep can work at all: drive identical inputs, print a
-- checksum of the sim state every frame, and require the two streams to be
-- byte-identical.
--
--   SLOT=a26_2k_4k ./run.sh stock det > build/det_stock.txt
--   ./run.sh tennis det > build/det_split.txt
--   python3 tools/ramdiff.py build/det_stock.txt build/det_split.txt
--
-- The input has to be generated HERE and not by hand, because the whole point
-- is that both runs see the same bytes at the same frames. It is a plain
-- function of a counter of SIM frames -- no randomness, nothing read back off
-- the screen, nothing that depends on how fast the emulator ran.
--
-- WHAT IT COVERS IS EVERYTHING, and that is a statement about Tennis rather
-- than a lazy default. The sibling ports had to carve cells out of their
-- ranges because some differ between two builds that are playing identically:
-- Video Olympics' $81 is stack-derived and the stack pointer means something
-- different either side of a bank switch, and its $84/$85 are raw analog
-- captures. Tennis has neither. It never executes TSX, it has no analog input,
-- and the highest cell it names is $D5 -- so $80-$D5 is exactly the game, and
-- $D6-$FF is exactly this port. Nothing in between needs excusing.
--
-- EXCEPT $8A, AND IT IS A REAL FINDING RATHER THAN A CONVENIENCE.
--
-- Tennis's score kernel reads one byte out of bounds, every frame, and the
-- 1981 cartridge depends on the result. The loop at $F506 counts Y down from
-- 6 and tests it AFTER decrementing:
--
--      $F526  DEY
--      $F527  LDA ($B2),Y
--      $F529  STA $8A
--      $F52B  TYA
--      $F52C  STA HMCLR
--      $F52E  BPL LF506
--
-- so the last iteration runs with Y = $FF and reads ($B3:$B2) + $FF. $B3 is
-- $F7 -- the score font's page -- so the address is $F8(B2-1), which is past
-- the end of a 2K cartridge. In the stock image MAME mirrors the 2K into the
-- 4K slot and it folds back to $F0(B2-1), a byte of the display kernel. In the
-- FujiNet cartridge $1800-$1FFF is the MAILBOX, so it lands on a text plane
-- instead, and an undriven plane reads $FF.
--
-- What $8A does with it is clamp: $F396 and $F3AB compare the racket's return
-- angle against it, and both comparisons only bite when $8A <= $0F. $B2 takes
-- twelve distinct values out of LF73F, and EXACTLY ONE of them -- $8D, which
-- reads $F08C = $08 -- is small enough to matter. So the divergence is one
-- score glyph wide, and in the banked build (where $8A is always $FF) that one
-- clamp never fires.
--
-- IT IS A FIDELITY DIVERGENCE AND NOT A DESYNC, which is the distinction
-- Combat's PORTING.md 4.4 insists on: both consoles run the identical banked
-- image and read the identical byte, so lockstep is untouched. What lockstep
-- DOES require is that the byte be the same on both -- which is why the boot
-- bank blanks the text planes before it hands over, rather than leaving
-- whatever words it last drew under the score kernel's feet.
--
-- `make glyph` is the gate for that half: it asserts the value is CONSTANT
-- across a run. This one excludes the cell and says why, because a gate that
-- has been taught to excuse the thing it found is not a gate -- but a gate
-- that reports a known, bounded, documented difference forever is not one
-- either.
local RANGES = {
    { 0x80, 0x89 },
    { 0x8B, 0xD5 },
}

-- SAMPLED AT THE TIMER ARM, and the choice is the whole design of the tap.
--
-- Video Olympics sampled at CXCLR, "after the game logic and before the
-- picture". TENNIS NEVER STROBES CXCLR -- it never touches a collision
-- register at all, which is the same fact that makes a stalled frame free --
-- so that landmark does not exist here.
--
-- $F1A2's `STY TIM64T` is the one that does. It is written EXACTLY ONCE per
-- frame, at the end of the picture and before the frame's logic, so what it
-- samples is "the state the previous frame computed and then drew". Both
-- builds reach it by the same path.
--
-- It also sidesteps the trap that cost the sibling a guard clause. Tennis's
-- RAM clear is `STY $00,X` with X sweeping upward, which WRAPS INSIDE PAGE
-- ZERO and strobes the whole TIA register file on the way past -- so a tap on
-- any TIA address fires spuriously on every RESET and every SELECT, with the
-- state still zeroed. TIM64T is at $0296, in the RIOT, and the clear never
-- reaches it. No skip rule, and therefore no skip rule to get subtly different
-- between two tools (PORTING.md's "two tools numbering frames differently cost
-- an afternoon").
local TIM64T = 0x0296

local sp = manager.machine.devices[":maincpu"].spaces["program"]
local frame = 0
local held = {}

-- CACHED ONCE. A field looked up through manager.machine.ioport.ports at the
-- moment of pressing it is a fresh wrapper, and set_value on a fresh wrapper is
-- lost -- the raw port never changes, and nothing reports an error.
local FIELDS = {}
local function field(tag, name)
    local key = tag .. "|" .. name
    if FIELDS[key] == nil then
        local p = manager.machine.ioport.ports[tag]
        FIELDS[key] = (p and p.fields[name]) or false
    end
    return FIELDS[key]
end

local function set(tag, name, on)
    local f = field(tag, name)
    if f then f:set_value(on and 1 or 0) end
end

-- THE STICKS. Tennis is a joystick game, so MAME's default `joy` in both slots
-- is right and run.sh passes nothing. The fields are the ones `./run.sh tennis
-- ports` prints, copied rather than guessed:
--
--   :joyport1:joy:JOY  "P1 Up"/"P1 Down"/"P1 Left"/"P1 Right"/"P1 Button 1"
--   :joyport2:joy:JOY  "P2 ..."
--
-- SWCHA's high nibble is the LEFT port and its low nibble the right, and
-- $F208's `CPX $D0 / BEQ` sends the high nibble to whichever court index
-- equals $D0. So port 1 here is the console's left port, which is the netcode's
-- port 0 and the host.
local P1 = ":joyport1:joy:JOY"
local P2 = ":joyport2:joy:JOY"

-- 1 IS A PRESS, for the console switches too. SWCHB is active low on the
-- hardware and MAME applies that inversion itself, so a harness that writes 0
-- to "press" SELECT is really releasing it, and the 1 it writes to "release"
-- is a press that then never ends.
--
-- DET_QUIET: start a game and then touch nothing.
--
-- It exists because injecting input through MAME's ports is NOT frame-exact
-- between two builds: the two boot through different amounts of code, so a
-- change can land on one side of one build's read and the other side of the
-- other's. A quiet run removes the variable -- the input streams are then
-- identical by construction, so any difference in state is the bank split's
-- and nothing else's -- and the whole frame loop, the timers, the sound, the
-- scoring and the kernel all still run.
local QUIET = os.getenv("DET_QUIET") ~= nil

local function drive(f)
    local want = {}

    -- RESET, to start a game rather than compare two attract screens. A gate
    -- that never presses anything proves nothing (PORTING.md 4.18), and a
    -- version of this harness in the family pressed RESET for its whole life
    -- from inside a memory tap, where set_value is silently lost, and never
    -- started a game once.
    if f >= 40 and f < 48 then want[":SWB|Reset Game"] = true end

    if f >= 90 and not QUIET then
        -- Both sticks, on different periods, so the two players are never
        -- doing the same thing and a swap between them would show.
        if (f // 37) % 4 == 0 then want[P1 .. "|P1 Left"] = true end
        if (f // 37) % 4 == 2 then want[P1 .. "|P1 Right"] = true end
        if (f // 23) % 5 == 0 then want[P1 .. "|P1 Up"] = true end
        if (f // 29) % 5 == 3 then want[P1 .. "|P1 Down"] = true end
        if (f // 41) % 4 == 1 then want[P2 .. "|P2 Left"] = true end
        if (f // 41) % 4 == 3 then want[P2 .. "|P2 Right"] = true end
        if (f // 31) % 5 == 2 then want[P2 .. "|P2 Up"] = true end
        if (f // 19) % 5 == 4 then want[P2 .. "|P2 Down"] = true end
        -- The trigger is the serve: `BIT $A0 / BPL` at $F363 only demands a
        -- press while a serve is pending, so a rally needs none and a game
        -- that never sees one never starts.
        if (f // 53) % 3 == 0 then want[P1 .. "|P1 Button 1"] = true end
        if (f // 71) % 3 == 0 then want[P2 .. "|P2 Button 1"] = true end
    end

    for k in pairs(held) do
        if not want[k] then
            local tag, name = k:match("^(.-)|(.*)$")
            set(tag, name, false)
            held[k] = nil
        end
    end
    for k in pairs(want) do
        if not held[k] then
            local tag, name = k:match("^(.-)|(.*)$")
            set(tag, name, true)
            held[k] = true
        end
    end
end

-- INPUT IS DRIVEN FROM A FRAME NOTIFIER, NOT FROM THE MEMORY TAP BELOW.
-- set_value called from inside a tap is silently lost -- the tap runs in the
-- CPU's execution context and port state settles at frame boundaries -- so the
-- raw port never changes and nothing reports an error. A harness that pressed
-- RESET this way for a hundred runs never started a game once, and every
-- comparison still passed, because two builds sitting in attract mode agree
-- just as well as two builds playing.
_G._det_drive = emu.add_machine_frame_notifier(function()
    drive(frame)
end)

_G._det_tap = sp:install_write_tap(TIM64T, TIM64T, "tim64t", function(off, data, mask)
    frame = frame + 1
    -- Rotate-and-add, not a plain sum: a plain sum cannot see two bytes that
    -- swapped, and two players that swapped is exactly the failure this is for.
    local c = 0
    for _, r in ipairs(RANGES) do
        for a = r[1], r[2] do
            c = ((c << 1) | (c >> 15)) & 0xFFFF
            c = (c + sp:readv_u8(a)) & 0xFFFF
        end
    end
    print(string.format("%d %04X", frame, c))
end)
