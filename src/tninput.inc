; tninput.inc -- the hook, and the once-a-frame shim.
;
; Tennis's whole input surface is five reads and emu/inputs.lua proves it
; rather than assuming it: after the patch, every read of a console port in a
; whole run comes from in here.
;
; It runs from TNWAIT at $F051, which is the spin that ends the one timed band
; this console has -- armed at $F1A2 with $2D, so 2880 cycles, of which the
; worst measured frame leaves 442. Everything below is therefore paid for out
; of slack the spin was going to burn anyway: IT COSTS THE FRAME NOTHING, and
; `make frames` is the gate that says so rather than the argument.

; ---------------------------------------------------------------------------
; TNWAIT -- the hook, called from $F051 in place of the stock spin.
;
; IT MUST RETURN WITH A = 0. $F05A clears VBLANK with `STA VBLANK` and the
; accumulator the stock spin left at zero when INTIM ran out. Every path out of
; here ends in TNWSPIN, which exits on `BNE` failing -- A = 0. That is the sort
; of thing which works for a thousand frames and then does not.
TNWAIT:
        jsr     TNSHIM

        lda     TNENT
        and     #TNE_NET
        beq     TNWSPIN         ; no match: the stock spin, unchanged

; The step loop. INTIM is re-read before every step and the loop stops when the
; slack is gone, so THE HOOK CANNOT OVERRUN THE KERNEL BY CONSTRUCTION -- there
; is no budget to get wrong.
;
; That matters more here than in the sibling ports. The spin below polls every
; 7 cycles and 7 is coprime with 256, so overrunning does not merely exit late:
; past underflow the RIOT decrements INTIM once per cycle, the poll can step
; straight over zero, and the wait becomes another 1800 cycles. Twenty-three
; scanlines, and a rolling picture.
;
; TIM64T decrements every 64 cycles, so TNGATE = 8 guarantees 7 x 64 = 448
; cycles remain against a worst step of about 310.
TNWLOOP:
        lda     INTIM
        cmp     #TNGATE
        bcc     TNWSPIN         ; THROUGH THE CARRY: INTIM is unsigned, and a
                                ;   sign test on an unsigned quantity is how the
                                ;   Channel F port lost a day
        jsr     TNNSTEP
        jmp     TNWLOOP

TNWSPIN:
        lda     INTIM
        bne     TNWSPIN
        rts

; ---------------------------------------------------------------------------
; TNSHIM -- once a frame, from TNWAIT.
;
; Returns with TNADV nonzero if the game logic should run this frame. The gate
; itself is TNSTGATE, patched in at $F1A5, which reads TNADV and either lets
; the logic run or jumps straight to the display half.
;
; IT RUNS BEFORE THE PICTURE, NOT BEFORE THE LOGIC, and the difference is worth
; stating because it is the one place Tennis's frame is shaped differently from
; the siblings'. The band is armed at the END of the kernel ($F1A2) and spun out
; at the TOP of the next frame ($F051), so the order in a pass is
;
;       arm -> game logic -> display setup -> SPIN (here) -> picture
;
; Everything this writes is read by the game logic of the NEXT pass. That makes
; the console's own stick about 192 scanlines staler than stock's live read at
; $F203 -- identically on both consoles, which is all lockstep asks -- and it
; is why the capture belongs here rather than at the band's head: at the head
; the transport would have no slack left to run in.
TNSHIM:
        lda     #$FF
        sta     TNADV           ; assume the sim advances

        IF      TNLAG
; The interception proof (`make lag`): refresh the shadows once every TNLAGM+1
; frames. Tennis then answers the stick in visible steps about a second apart
; -- and if it does not, something is still reading a live port.
        lda     $84
        and     #TNLAGM
        beq     TNSHIM1
        rts
TNSHIM1:
        ENDIF

        lda     TNENT
        and     #TNE_NET
        bne     TNNET

; Not in a match: the console drives both players, exactly as it always did.
        jmp     TNLOC0
