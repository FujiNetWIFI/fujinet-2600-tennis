; tnkern.asm -- bank 2: Tennis's display half, and the netcode.
;
; The picture is 556 bytes of code and 385 of data, so this bank has about a
; kilobyte spare -- which is why the whole network state machine lives here.
; The mailbox at $1D00-$1FFF is fixed and visible from every bank, so the
; netcode does not care which bank it runs in; it should run where the room is.
;
; It is also where it has to be. The hook is the spin at $F051, which is in
; this bank, so the transport is reached without a bank switch and the shim's
; decision reaches the game logic the only way it can: through a RAM cell,
; TNADV, written here and read there, later in the same frame.
;
; THE ENTRY IS A TAIL CALL, NOT A CALL. Both seams are fall-throughs in the
; stock frame, so neither had to change shape: bank 1 reaches $1015 and bank 2
; reaches $1168, and each simply carries on where the other stopped.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "tndefs.inc"
        INCLUDE "tail.inc"

TNBANK  EQU     BANKKERN

; ---------------------------------------------------------------------------
; The entry, in the 21 bytes bank 1's dispatcher occupies over there.
;
; The trampoline enters every bank at $1000 and this bank has exactly one way
; in -- seam A -- so the stub is one jump. CLD is insurance rather than
; ceremony: an ADC anywhere in the network machine that ran in decimal mode
; would be a desync that only showed up after a score.
        ORG     $1000
TNKENT: cld
        jmp     LF015

; ---------------------------------------------------------------------------
; Seam B: display -> game, at the fall-through out of the 24-line tail spin.
;
; $1168 is a hole here because `LDX #$03` and everything after it belongs to
; bank 1. Putting the boundary at $F168 rather than $F16C is what makes this
; free: X is loaded on the far side, so nothing has to survive TNGOTO's clobber.
; Y does survive, and must -- the spin leaves it at 0 and $F17B's `DEY` turns
; that into $FF -- and TNGOTO never touches Y.
;
; The switch costs about 35 cycles and the very next instruction over there is
; `STX WSYNC`. The spin exits 7 cycles into a scanline, so the store lands 42
; cycles in and still waits out the same line: the frame does not change length.
        ORG     TNSEAMB
        lda     #BANKGAME
        jmp     TNGOTO

        INCLUDE "tninput.inc"
        INCLUDE "tncap.inc"
        INCLUDE "tnnet.inc"

        IF      * > $1497
        ERROR   "seam B and the netcode have overrun the sprite positioner at $1497"
        ENDIF

        INCLUDE "tennis.inc"

        END
