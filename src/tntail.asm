; tntail.asm -- the fixed half: the trampoline, the shared transport, and the
; cold stub.
;
; $1800-$1F1F is the cartridge's mailbox -- text planes, reply window, control
; page, TX page, status -- and the cartridge paints it. The client owns
; $1F20-$1FFB, which fuji_mailbox.h calls the fixed tail, plus the vectors.
;
; Everything here has to be at an address that does not move, for three
; different reasons:
;
;   * The store that switches bank is the LAST instruction fetched from the
;     old bank and the very next fetch comes from the new one, so the jump
;     after it cannot live in a bank.
;   * This console has no reset line to the cartridge. The RESET switch
;     restarts the 6507 with whatever bank was last selected still mapped, so
;     a cold stub living in bank 0 would simply not be there when it was
;     needed.
;   * The transport is the same bytes in all three banks, and a bank is 2048.
;     Here it is one copy that all of them can reach.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "tndefs.inc"

; The tail has no bank identity -- it IS the shared copy -- but it is assembled
; next to the same equates every bank uses.
TNBANK  EQU     BANKBOOT
TNHASTXT EQU    1               ; the tail carries the text primitives for now;
                                ;   if it ever runs short they move into bank 0,
                                ;   which is the only bank that draws text

; TNGOTO is not a label: tndefs.inc gives it a fixed address and this ORG is
; what makes that true, so the two cannot drift apart. It must be FIRST in the
; tail, because it is the one address a bank has to know before build/tail.inc
; exists.
        ORG     TNGOTO

; ---------------------------------------------------------------------------
; TNGOTO -- select bank A and enter it at $1000.
;
; ONE store. FN_HOT_BANK lives in the bit-7-set half of the control page, which
; is the one-shot half: the bank number is in the ADDRESS and the data is
; ignored. A store to $1DFF afterwards would be FN_H_COMMIT, and it would
; commit whatever FN_REG_* was last armed, carrying this store's value.
;
; `sta FNRSEL,x` is the documented-safe indexed form: the base low byte is $00,
; so the index cannot carry and the dummy read that STA abs,X always performs
; lands on the same address as the write -- one parked access, not two.
;
; IT CLOBBERS A AND X AND LEAVES Y ALONE, and seam B depends on exactly that.
; The kernel bank falls out of its 24-line tail spin with Y = 0, and $F17B's
; `DEY` in the game bank turns that into $FF; nothing here touches Y. X is the
; other half of the same story, which is why the seam is at $F168 and not
; $F16C -- see tndefs.inc.
        clc
        adc     #FH_BANK
        tax
        sta     FNRSEL,x
; RESET THE STACK. A bank switch is a JUMP and nothing ever returns through
; one, so every switch abandons whatever return addresses were on the stack.
; Tennis wants SP = $FF at the top of its frame loop anyway -- its own clear
; loop does exactly this, a TXS per iteration ending at $FF -- so here the
; reset is both free and correct.
;
; The store above has already switched the bank; these instructions are fetched
; from the FIXED tail, which is not banked, so they still execute.
        ldx     #$FF
        txs
        jmp     $1000

; ---------------------------------------------------------------------------
; The shared transport. tools/mktail.py turns the addresses these assemble to
; into build/tail.inc, which is what the banks include.
        INCLUDE "tncore.inc"

; ---------------------------------------------------------------------------
; TNCOLD -- power-on and RESET.
;
; Deliberately tiny. All that has to be here is what cannot be anywhere else:
; the arming pair, because banking is a control-page operation and that page
; decodes nothing until an ordered pair of stores carrying two specific values
; arrives -- and the bank switch itself.
;
; SEI and CLD are here because this is where stock did them and there is
; nowhere else left: $F000-$F002 became the game bank's four-byte entry
; dispatcher, and four bytes is exactly `bit TNWARM / bmi`. They are cold-path
; work and this is the cold path.
;
; It zeroes TNENT and TNWARM. This console does not clear its RAM on a reset,
; so the bytes that say what the bank being entered should do still hold
; whatever the last frame set them to; without this, a RESET taken mid-match
; comes back into the frame loop with a sim tick, a ring and a socket that no
; longer mean anything -- and, worse, with TNWARM still set, so the game bank
; would resume a frame instead of starting one.
TNCOLD: sei
        cld
        ldx     #$FF
        txs
        lda     #0
        sta     TNENT
        sta     TNWARM
        lda     #FNAM1
        sta     FNRSEL+FH_ARM1
        lda     #FNAM2
        sta     FNRSEL+FH_ARM2
        lda     #BANKBOOT
        jmp     TNGOTO

; ---------------------------------------------------------------------------
; THE VECTORS, AND WHY BOTH OF THEM ARE SPARE HERE.
;
; Video Olympics could not do this: it issues BRK three times as a two-byte
; subroutine call, so its IRQ vector had to be pointed back into the bank
; holding the handler. Tennis issues no BRK at all -- tools/checkmap.py asserts
; it on every build -- and SEI runs before anything else, so neither vector is
; ever taken and both can point at the cold stub.
;
; That is worth asserting rather than assuming for a second reason: in the
; STOCK image $F7FE/$F7FF are not a vector either. They are the two
; difficulty-switch masks, $40 and $80, that $F1FC reads as `AND LF7FE,X`.
; David Crane reclaimed the vector as data. Here the real vectors are up here
; and those two bytes stay exactly what the game reads -- see tools/mkbanks.py.
        ORG     $1FFC
        DW      TNCOLD          ; RESET
        DW      TNCOLD          ; IRQ/BRK -- never taken; there is no BRK

        END
