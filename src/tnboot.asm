; tnboot.asm -- bank 0: the cold start and, later, the session.
;
; For now it is the handover and nothing else: no socket, no screens, no
; appkeys. `make det` and `make frames` are gates about the SPLIT, and they
; have to run before there is any netcode for a difference to be blamed on.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "tndefs.inc"
        INCLUDE "tail.inc"

TNBANK  EQU     BANKBOOT

        ORG     $1000
TNBENT:
; Zero the whole of RAM and the TIA with it, exactly as stock's own START does
; and for the same reason -- `STY $00,X` wraps inside page zero. Counting UP
; from $FF would wrap past $FF into the TIA on the way, which strobes WSYNC and
; RESP0; counting DOWN from $7F over base $80 does not.
        lda     #0
        ldx     #$7F
TNBCLR: sta     $80,x
        dex
        bpl     TNBCLR
        sta     TNENT
        sta     TNWARM
        lda     #BANKGAME
        jmp     TNGOTO

        IF      * > $1800
        ERROR   "the boot bank has overrun the mailbox"
        ENDIF

        END
