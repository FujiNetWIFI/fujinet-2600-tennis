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

# No input patches yet. Milestone 2 is the split alone: the game still reads
# its own console, and `make det` has to prove the split plays exactly like the
# 1981 cartridge before there is any netcode for a later difference to be
# blamed on.
INPUTS = []

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
SPANS = []


# Two addresses DiStella never labelled, because nothing branches to them.
# They are spelled out in tndefs.inc as TNSPIN and TNRESUM, and these are the
# opcodes that must be there -- so a typo in either equate is a build error
# rather than a jump into the middle of an instruction.
LANDINGS = [
    (0xF168, 0xA2, "TNRESUM: LDX #$03, where the game bank resumes after the "
                   "display half's 24-line tail"),
    (0xF051, 0xAD, "TNSPIN: LDA INTIM, the spin that ends the one timed band"),
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
