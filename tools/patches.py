"""patches.py -- the declared patch map for networked Tennis.

The family's discipline: the original source is never edited, every change to
it is DECLARED here, and tools/check_patch.py fails the build on any difference
between the built image and the cartridge dump that is not on this list. A
patch that changes nothing and a change that was never declared are both build
failures.

Each entry is anchored on a LINE NUMBER of rom/tennis.asm plus the exact text
that line must contain. That file is GENERATED -- `make disasm` runs DiStella
over the dump with tools/tennis.cfg -- so its line numbers move whenever the
code/data map changes. The `old` text check is what makes that safe: a shifted
line is a loud build error rather than a silent mis-patch. If tools/tennis.cfg
changes, expect to re-anchor this file, and let the errors tell you where.

`addr` is where the patch lands in the stock ROM, for check_patch.py's byte
audit. `size` is 0 when the patch is the same length as what it replaces.

EVERY PATCH BEFORE $F051 MUST BE CYCLE-PRESERVING AS WELL AS SIZE-PRESERVING.
LF4A3 positions sprites by counting cycles and then strobing RESP0/RESP1 --
and RESPx fixes an object's X from the raster position at the instant it
executes. It is called three times from $F019-$F026, inside the window between
the timer arm at $F1A2 and the spin at $F051. Video Olympics 3.20 is the
account of what it costs to learn this the other way round: one cycle, found
only at thirty seconds, in a gate that had passed at ten for its whole life.
"""

# ---------------------------------------------------------------------------
# Milestone 2: the bank split. Structural only -- the game still reads its own
# console, so a split build must play EXACTLY like stock.
# ---------------------------------------------------------------------------

STRUCTURAL = [
    dict(
        name="START becomes the bank entry dispatcher; the clear moves out and "
             "gains a bound",
        line=50, nlines=13, addr=0xF000, size=0,
        old="""START:
\tSEI
\tCLD
\tLDX    #$00
LF004: LDY    #$00
LF006: STY    VSYNC,X
\tTXS
\tINX
\tBNE    LF006
\tSTX    AUDV0
\tLDA    #$11
\tSTA    CTRLPF
\tJSR    LF5E7   """,
        new="""; $1000 is where a bank switch lands, and this bank is entered there twice:
; once cold, from the boot bank, and once every frame on the way back from
; the display half. The dispatcher has to tell those apart, and it has
; EXACTLY FOUR BYTES in which to do it -- because $1004 is LF004, and
; `JMP LF004` at $F1AB is a stock instruction this port does not patch.
;
; `bit TNWARM / bmi` is four bytes. Testing a bit of TNENT instead would
; cost seven: BIT can only reach bits 6 and 7 without a mask, and in this
; family those two are the tick phase. Hence TNWARM's own cell.
        bit     TNWARM
        bmi     TNWRES

; The clear leaves. It cannot stay: `STY VSYNC,X` wraps inside page zero and
; the loop runs X up to $FF, so it covers all 128 bytes of RAM -- and there
; are THREE ways in, cold with X = $00 and `LDX #$85 / JMP LF004` on RESET
; and `LDX #$88 / JMP LF004` on SELECT. The last two happen several times a
; minute while somebody is picking a game. Unbounded, every netcode cell
; would go with them, and not rarely: normally.
;
; TNCLR is the same loop with a bound and a stated stack pointer. It lives in
; the hole the display half left, where there is no size pressure, and the
; address LF004 keeps is a jump to it -- so both stock re-entries still
; arrive exactly where they always did, with the seed still in X.
LF004:  jmp     TNCLR

; And the warm resume: bank 1 re-enters at $1000 every frame, and what it
; wants is $1168, where the game half picks up after the display half's
; 24-line tail. TNRESUM is spelled out in tndefs.inc and its opcode is
; asserted below, so a typo is a build error and not a jump into the middle
; of an instruction.
TNWRES: jmp     TNRESUM"""),
]

# ---------------------------------------------------------------------------
# Milestone 3: the shim. Every console port read becomes a read of a RAM
# shadow, and the shadows are filled in one place -- TNLOC0 locally, TNMIX in a
# match. `make inputs` is the proof: after this, every read of a port in a whole
# run comes from the shim and the game's own five sites are gone.
#
# EVERY SWCHA/SWCHB SITE IS ABSOLUTE, so redirecting it to a zero-page shadow
# through ABSOLUTE addressing keeps all three bytes. `>` is AS's force-long
# prefix; without it `LDA TNSWB` assembles to two bytes, every byte after it
# moves, and check_patch reports the entire rest of the bank rather than the one
# thing that is wrong. (AS has `<`/`>` as ADDRESSING-MODE prefixes and not as
# lo/hi-byte operators -- two different things wearing the same character.)
# ---------------------------------------------------------------------------

INPUTS = [
    dict(
        name="$F04C: the hook, in FRONT of the spin rather than instead of it",
        line=88, nlines=2, addr=0xF04C, size=0,
        old="""\tLDA    LF73F,X
\tSTA    $B2     """,
        new="""; THE STOCK SPIN AT $F051 IS NOT TOUCHED, and that is the whole point of
; hooking here instead of there.
;
; The obvious patch replaces `LDA INTIM / BNE LF051` outright, and the first
; two versions of this port did. Both were wrong in the same way and it took
; `make frames` at twenty seconds to show it: whatever replaces those five
; bytes has to get back to $F056 somehow, and a JSR's RTS or a JMP costs
; cycles that are spent AFTER THE TIMER HAS EXPIRED, immediately in front of
; `STA WSYNC`. Almost always free -- the store waits out the line anyway --
; and about one frame in six hundred enough to push it past the end of the
; line, giving a 263-line frame among 262s. JSR and two NOPs cost ten cycles
; and did it every few seconds; JMP and JMP cost three and did it every four.
;
; Hooking in FRONT of the spin costs nothing at all. Everything below runs
; while the timer is still going, and the spin absorbs all of it by
; construction -- which is the same reason the whole shim lives in a timed
; band. The two NOPs are the two bytes `STA $B2` left over, and they are
; before the spin too.
;
; TNWAIT does `LDA LF73F,X / STA $B2` itself, first, so the glyph pointer is
; set exactly as it was and X is still $86's value when it reads the table.
\tJSR  TNWAIT
\tNOP
\tNOP"""),

    dict(
        name="$F170: the frame counters move behind the lockstep gate",
        line=237, nlines=6, addr=0xF170, size=0,
        old="""	INC    $84
	BNE    LF17B
	INC    $88
	BNE    LF17B
	SEC
	ROR    $88     """,
        new="""; Eleven bytes become three and eight of filler. These two counters pace the
; simulation -- $84 bit 0 is the half-speed frame skip for variations 2 and 3,
; $84 bit 6 dithers the computer opponent, $88 is the attract timeout -- and
; they sit ahead of the gate, so in stock they advance on every frame the
; television draws. Behind the gate they advance once per tick that RAN, which
; is what two consoles can agree about.
;
; A JUMP AND NOT A CALL, and the nine cycles that saves are load-bearing.
;
; THIS IS THE ONE PIECE OF THE SHIM THAT IS NOT INSIDE A TIMED BAND. The band
; is armed at $F1A2, twenty-six bytes further on, so everything here is paid
; for out of the scanline that `STA WSYNC` at $F199 is about to end -- and a
; line is 76 cycles. Stock spends about 45 of them on the frame $84 wraps and
; $88 has to be incremented too. `JSR / ... / RTS / JMP LF17B` spends fifteen
; more than `JMP / ... / JMP`, and that was enough: one 263-line frame among
; 262s every 256 frames, which is exactly the counter's wrap, arriving every
; 4.3 seconds like clockwork. `make frames` at twenty-five seconds is what
; showed the period; at three it showed nothing at all.
;
; THE FILLER IS ONLY INERT BECAUSE NOTHING REACHES IT. $FF is not a NOP: it is
; the undocumented ISC abs,X, a read-modify-write, and `ISC $FFFF,X` with the
; X = $03 that is live here addresses $0002 -- WSYNC, strobed three times per
; instruction. An earlier version of this patch left the padding reachable
; after a JSR, measured 268 scanlines, and painted the screen black because
; $83, the attract colour mask, was one of the cells the runaway wrote.
;
; $F17B is a branch target twice over and has to stay where it is, so the
; padding stays too; the jump past it is what makes it harmless.
	JSR  TNFCNT
	JMP  LF17B
	DB   $FF,$FF,$FF,$FF,$FF"""),

    dict(
        name="$F1A5: the stall gate, in front of the RESET and SELECT tests",
        line=266, nlines=3, addr=0xF1A5, size=0,
        old="""	PLA
	LSR
	BCS    LF1AE   """,
        new="""; Four bytes become three and one of filler. TNSTGATE pulls the SWCHB byte
; $F17F pushed -- unconditionally, because the PHA runs on every frame the
; television draws and a gate that leaked a byte of stack per stalled frame
; would be a wild jump inside two seconds -- and then either lets the logic
; run or jumps straight to the display half.
;
; $F1A9 and $F1AE are reached by name (TNRST, TNSEL) rather than by falling
; through, and both are still stock bytes: $F1AB is `LF1AB`, a branch target
; from $F1D4, so this patch stops at $F1A8.
	JMP  TNSTGATE
	DB   $FF"""),

    dict(
        name="$F1B9: SELECT, whitelisted to the two-human variations",
        line=277, nlines=5, addr=0xF1B9, size=0,
        old="""LF1B9: LDY    $80
\tINY
\tCPY    #$04
\tBCC    LF1C2
\tLDY    #$00    """,
        new="""; Nine bytes become three and six of unreachable filler. $80 runs 0-3 and
; $81 is $80 AND 1 -- the two-humans flag that $F210 tests to skip the
; sixty-nine bytes at $F212 which synthesise a joystick nibble for the
; computer. Over a network a computer on one side is not a match, and it is
; the one place in the game that invents input.
;
; TNVAR steps by TWO in a match, so the parity holds and only the odd
; variations are reachable; locally it steps by one and walks all four, which
; is what keeps `make det` a comparison against the 1981 cartridge.
;
; $F1C2 is `LF1C2`, a branch target from $F1BE in stock and reached by name
; from TNVAR here, so this patch stops one byte short of it.
LF1B9:\tJMP  TNVAR
\tDB   $FF,$FF,$FF,$FF,$FF,$FF"""),

    dict(name="$F02B: LDA SWCHB -> LDA TNSWB (the colour and B&W tables)",
         line=73, nlines=1, addr=0xF02B, size=0,
         old="LF02B: LDA    SWCHB   ",
         new="LF02B:	LDA  >TNSWB            ; was SWCHB"),

    dict(name="$F17C: LDA SWCHB -> LDA TNSWB (pushed, for RESET and SELECT)",
         line=244, nlines=1, addr=0xF17C, size=0,
         old="	LDA    SWCHB   ",
         new="	LDA  >TNSWB            ; was SWCHB"),

    dict(name="$F1F9: LDA SWCHB -> LDA TNSWB (the per-player difficulty)",
         line=310, nlines=1, addr=0xF1F9, size=0,
         old="	LDA    SWCHB   ",
         new="	LDA  >TNSWB            ; was SWCHB"),

    dict(name="$F203: LDA SWCHA -> LDA TNSWA (both sticks, once per player)",
         line=314, nlines=1, addr=0xF203, size=0,
         old="	LDA    SWCHA   ",
         new="	LDA  >TNSWA            ; was SWCHA"),

    dict(
        name="$F376: LDY INPT4,X -> LDY TNTRIG,X, the only trigger read",
        line=521, nlines=1, addr=0xF376, size=0,
        old="	LDY    REFP1,X ",
        # DiStella names the register by the address, and $0C is INPT4 on a
        # READ and REFP1 on a WRITE. The shadow pair is zero page in the RIOT
        # where INPT4 is zero page in the TIA, so this is the same opcode and
        # the same four cycles. X is the PORT index: $F364-$F368 computes
        # `$CA EOR $D0`, which turns the serving player's court index into the
        # console port their trigger is actually plugged into.
        new="	LDY  TNTRIG,X          ; was INPT4,X (DiStella spells $0C REFP1)"),
]

# ---------------------------------------------------------------------------
# REWRITTEN -- ranges where wholesale change is expected and byte-level
# auditing would be noise. Exactly one: the 21 bytes of cold-boot head.
# ---------------------------------------------------------------------------

REWRITTEN = [
    (0xF000, 0xF015, "START becomes the bank entry dispatcher; the RAM clear "
                     "moves to TNCLR, which stops at TNZPLO"),
]

# SPANS -- every other changed byte, as (address, length, why). check_patch.py
# allows a difference here and NOWHERE else, and fails if any of these spans
# turns out to be unchanged.
SPANS = [
    (0xF02B, 3, "LDA SWCHB -> LDA TNSWB"),
    (0xF1B9, 9, "SELECT, whitelisted to the two-human variations"),
    (0xF04C, 5, "the hook, in front of the spin rather than instead of it"),
    (0xF170, 11, "the frame counters move behind the lockstep gate"),
    (0xF17C, 3, "LDA SWCHB -> LDA TNSWB"),
    (0xF1A5, 4, "the stall gate, in front of the RESET and SELECT tests"),
    (0xF1F9, 3, "LDA SWCHB -> LDA TNSWB"),
    (0xF203, 3, "LDA SWCHA -> LDA TNSWA"),
    (0xF376, 2, "LDY INPT4,X -> LDY TNTRIG,X"),
]


# Two addresses DiStella never labelled, because nothing branches to them.
# They are spelled out in tndefs.inc as TNSPIN and TNRESUM, and these are the
# opcodes that must be there -- so a typo in either equate is a build error
# rather than a jump into the middle of an instruction.
LANDINGS = [
    (0xF168, 0xA2, "TNRESUM: LDX #$03, where the game bank resumes after the "
                   "display half's 24-line tail"),
    (0xF051, 0xAD, "the stock spin, which this port does NOT patch: the hook "
                   "goes in front of it so that every cycle it costs is spent "
                   "while the timer is still running"),
    (0xF1A9, 0xA2, "TNRST: LDX #$85, stock's RESET arm"),
    (0xF1AE, 0x4A, "TNSEL: LSR, stock's SELECT test"),
    (0xF1C2, 0x84, "TNVARX: STY $80, where the SELECT handler resumes"),
]


def check_landings(rom):
    """rom is the stock 2048-byte dump."""
    for addr, want, why in LANDINGS:
        got = rom[addr - 0xF000]
        if got != want:
            raise SystemExit(
                "patches: %s -- expected $%02X at $%04X, found $%02X. "
                "The gate would jump into the middle of an instruction."
                % (why, want, addr, got))


def _norm(text):
    """Collapse whitespace. The anchor's job is to catch a MOVED LINE, not to
    police column alignment: rom/tennis.asm is generated by DiStella, which
    pads mnemonics to a fixed width with spaces, and hand-transcribing that
    padding into this file would make every entry a formatting puzzle with no
    safety gained. Token sequence is what identifies the line."""
    return " ".join(text.split())


def _check(out, p):
    """Verify the anchor before touching anything."""
    i = p["line"] - 1
    n = p.get("nlines", 1)
    if i < 0 or i + n > len(out):
        raise SystemExit(
            "patches: %s -- line %d is past the end of the source (%d lines). "
            "tools/tennis.cfg probably changed; re-anchor."
            % (p["name"], p["line"], len(out)))
    want = _norm(p["old"])
    have = _norm("".join(out[i:i + n]))
    if have != want:
        raise SystemExit(
            "patches: %s -- anchor mismatch at line %d\n  want: %s\n  have: %s"
            % (p["name"], p["line"], want, have))


def apply(lines):
    """Apply every declared patch.

    THE LINE COUNT IS PRESERVED, deliberately. tools/mkbanks.py maps every
    source line to the address the assembler put it at, using the listing of
    the UNPATCHED source, and then walks the patched lines by the same index.
    A patch that collapsed thirteen lines into one would shift every line after
    it and silently file half the game into the wrong bank.

    So the whole replacement -- however many lines of it there are -- goes into
    the FIRST slot, and the rest are blanked.

    Note this is about SOURCE lines, not bytes. Keeping the byte count is a
    separate obligation and it is on the patch author: every region is emitted
    at its own ORG and then simply runs on, so a replacement one byte short of
    what it replaces moves every byte after it, and check_patch reports the
    entire rest of the bank rather than the one thing that is wrong.
    """
    out = list(lines)
    for p in STRUCTURAL + INPUTS:
        _check(out, p)
        i = p["line"] - 1
        n = p.get("nlines", 1)
        out[i] = p["new"] + "\n"
        out[i + 1:i + n] = ["\n"] * (n - 1)
    return out
