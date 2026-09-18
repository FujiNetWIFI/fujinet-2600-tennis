; tnboot.asm -- bank 0: the cold start and the session.
;
; Everything that happens once, before a match, and nothing that happens during
; one. It is the only bank that may use the session half of the zero-page union
; -- FNDEV, FNCMD, FNNPR, FNTMO, FNCNT and the path cursor all share addresses
; with the lockstep rings, and the two are never live at the same time because
; this bank finishes before the game bank starts.
;
; It is also the only bank with a text kernel, which is why TNGONE hands back
; here to say "OPPONENT HAS LEFT" in words.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "tndefs.inc"
        INCLUDE "tail.inc"

TNBANK  EQU     BANKBOOT

        ORG     $1000
        INCLUDE "tnsess.inc"
        INCLUDE "tnappk.inc"
        INCLUDE "tndisp.inc"

        IF      * > $1800
        ERROR   "the boot bank has overrun the mailbox"
        ENDIF

        END
