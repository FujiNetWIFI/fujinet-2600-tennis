; tngame.asm -- bank 1: Tennis's frame loop, game logic, scoring, and the shim.
;
; Everything the game does between the end of the picture and the start of the
; next one. The display half is in bank 2, because Tennis is exactly 2048 bytes
; and a bank is exactly 2048 bytes, so something had to move to make room for
; netcode -- and the display half is the one with the room.
;
; Every byte here keeps the address it has in the cartridge dump, rebased
; $F000 -> $1000. The holes are the regions that went to bank 2:
;
;   $1015-$1167   339 bytes, where the display setup and the kernel were
;   $1497-$153A   164 bytes, where the sprite positioning was
;   $1618-$16FF   231 bytes, where the per-half setup and the bitmaps were
;   $172D-$175C    48 bytes, the glyph-pointer and colour tables
;   $176D-$17FD   145 bytes, the score font
;
; The two BOTH regions are emitted HERE as well and are therefore not free.
;
; THIS BANK OWNS $17FE-$17FF, and they are not vectors. `AND LF7FE,X` at $F1FC
; reads them as the two difficulty-switch masks; the real vectors live in the
; fixed tail. See tools/mkbanks.py.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "tndefs.inc"
; The shared transport's addresses, generated from the tail's own listing so
; there is no hand-kept list to go stale.
        INCLUDE "tail.inc"

TNBANK  EQU     BANKGAME

        INCLUDE "tennis.inc"

; ---------------------------------------------------------------------------
; The first hole: 339 bytes where the display setup and the kernel were. It is
; entered at a literal and not at a region-end label, because seam A's
; trampoline must be at $1015 exactly -- that is the address the stock game
; jumps to and falls into.
        INCLUDE "tnstart.inc"

        IF      * > $1168
        ERROR   "hole A has overrun the resume point at $1168"
        ENDIF

        END
