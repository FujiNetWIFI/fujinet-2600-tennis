; tngate.inc -- the game bank's half of the shim: the stall gate and the two
; counters that had to move behind it.
;
; The shim proper runs in the kernel bank, where the slack is. What has to be
; here is everything that decides whether the GAME LOGIC runs, because the game
; logic is here.

; ---------------------------------------------------------------------------
; TNSTGATE -- the lockstep gate, patched in at $F1A5.
;
; Stock is `PLA / LSR / BCS LF1AE`: pull the SWCHB byte pushed at $F17F, and
; test RESET then SELECT. The patch is three bytes over four, so this has to
; reproduce all of it.
;
; THE PLA IS UNCONDITIONAL AND THAT IS THE WHOLE POINT. It balances the PHA at
; $F17F, which runs on every frame the television draws, stalled or not. A gate
; that skipped it would leak one byte of stack per stalled frame, and with six
; bytes kept for the stack that is a wild jump inside two seconds of a peer
; being slow -- which would read as a transport fault rather than as what it is.
;
; X IS ZERO HERE IN STOCK AND MUST LEAVE ZERO. The WSYNC loop at $F199 counts
; X down to 0, and $F1D6's `STX $A3` -- the SELECT auto-repeat delay -- is the
; instruction that consumes it, twenty-nine bytes later with nothing in between
; reloading it. That is the Dragster 3.1 shape, and the only reason it is cheap
; here rather than expensive is that it was looked for.
TNSTGATE:
        pla                     ; the SWCHB byte $F17F pushed -- ALWAYS
        ldx     TNADV
        beq     TNSTALL
        ldx     #$00            ; restore what $F1D6 consumes
        lsr     a
        bcs     TNSTG1          ; bit 0 = RESET not pressed -> the SELECT test
        jmp     TNRST           ; $F1A9: LDX #$85 / JMP LF004
TNSTG1: jmp     TNSEL           ; $F1AE: LSR / BCS LF1D6

; ---------------------------------------------------------------------------
; TNSTALL -- the peer's input for this tick has not arrived.
;
; Nothing else happens. The frame still runs: VSYNC still goes out, the band's
; timer still runs, and the kernel still draws. What does not happen is the
; game logic, so the picture freezes coherently and the display never shudders.
;
; That is FREE on this console, and for Dragster's reason rather than Combat's.
; Tennis never strobes CXCLR and never reads a collision register at all -- its
; collisions are coordinate arithmetic, `SBC $8F / CMP #$08` and its siblings --
; and the display setup at $F015 rebuilds every TIA register the picture needs
; from RAM on every frame. N frozen frames leave exactly what one would, so two
; consoles that stalled for different lengths resume in agreement.
;
; The jump is to seam A, which is where the logic would have gone anyway:
; $F494's `JMP LF015`. So a stalled frame takes the same road out, just
; earlier.
TNSTALL:
        ldx     #$00
        jmp     TNSEAMA

; ---------------------------------------------------------------------------
; TNFCNT -- the two counters that pace the simulation, moved behind the gate.
;
; `INC $84` at $F170 and `INC $88` at $F174 sit in the VSYNC block, ahead of
; the gate, so in stock they advance on every frame the TELEVISION DRAWS. That
; is not decoration:
;
;   $84 bit 0  is the half-speed frame skip. $F1EC tests it, and variations 2
;              and 3 run the whole logic chain only on even frames.
;   $84 bit 6  dithers the computer opponent's vertical jitter at $F214.
;   $88        is the attract timeout, and bit 7 of it turns attract on.
;
; A counter of frames DRAWN rather than ticks RUN is a function of the local
; stall pattern -- and the stall pattern is the one thing two consoles are
; guaranteed to disagree about, because absorbing it is what lockstep is for.
; Video Olympics found the same rule from the other end and paid 143 CRC
; mismatches for it on a pair that was otherwise in perfect step.
;
; STOCK'S OWN CONDITION IS COPIED, NOT ITS INTENT. $88 advances only when $84
; wraps and the ROR only when $88 wraps, exactly as the two `BNE LF17B` did;
; a plain rewrite that ran them on a different test would diverge somewhere
; a hundred frames in, by one.
TNFCNT:
; ---- the byte the score kernel read past the end of its font ----
;
; $F527's `LDA ($B2),Y` runs once per frame with Y having reached $FF and lands
; at ($B3:$B2) + $FF. The address is a CONSTANT: $F14D is `SEC / LDA #$F6 /
; LDX #$06` and the loop at $F154 writes that literal into $B2 eight bytes
; before the call, on every frame, whatever the score. So the read is
; $F7F6 + $FF = $F8F5 -- past the end of a 2K cartridge.
;
; Stock folds that back to $F0F5 through the mirror. The FujiNet cartridge puts
; the MAILBOX at $1800-$1FFF, so it lands on a text plane and the value becomes
; whatever the boot bank last drew. That is not decoration: it lands in $8A, and
; $F396 and $F3AB use $8A to clamp the range of return angles a racket can
; produce. Measured -- undriven planes read $FF, the clamp never fires, and
; `make det` runs 2384 frames identical; blanked planes read $00, the clamp
; fires on every return, and det diverges at frame 43.
;
; HERE AND NOT IN THE STALL GATE, and the difference is the whole of Video
; Olympics' "reading a cell at the wrong moment is worse than not reading it".
; Either place is before the game logic reads $8A, so either would make the
; GAME right. Only this one is before the timer arm at $F1A2, which is where
; `make det` and `make glyph` sample -- so only this one makes the OBSERVER
; right as well, and a gate that reports a difference the game does not have is
; worse than no gate.
;
; It runs unconditionally, because stock's kernel writes $8A on every frame the
; television draws, stalled or not.
;
; TNFCNT IS ENTERED BY JMP AND LEAVES BY JMP. This is the one piece of the shim
; outside a timed band -- $F1A2 arms the timer twenty-six bytes later -- so its
; cycles come out of the scanline `STA WSYNC` at $F199 is about to end, and a
; line is 76. A call and a return cost nine more than two jumps, and nine was
; the difference between a constant 262-line frame and a 263 every 256 frames.
        lda     #TNMIRRV
        sta     $8A

        lda     TNADV
        beq     TNFC9
        inc     $84
        bne     TNFC9
        inc     $88
        bne     TNFC9
        sec
        ror     $88
TNFC9:  jmp     LF17B

; ---------------------------------------------------------------------------
; TNVAR -- SELECT, whitelisted to the variations two humans can actually play.
;
; $80 runs 0-3 and $81 is $80 AND 1. $81 is the two-humans flag: $F210's
; `LDY $81 / BNE LF25C` is what skips the sixty-nine bytes at $F212 that
; SYNTHESISE a joystick nibble for the computer opponent. Bit 1 of $80 is a
; separate thing entirely -- half speed, the logic chain running only on even
; frames.
;
; So of the four variations, two put a computer on one side of the net. Over a
; network that is not a match, and it is worse than pointless: it is the one
; place in the game that invents input, and the whole design rests on input
; coming from exactly two places.
;
; The whitelist is therefore not a table but a parity: IN A MATCH SELECT STEPS
; BY TWO, so an odd variation stays odd, and the wrap goes to 1 rather than 0.
; Locally it steps by one and walks all four, which is what keeps `make det` a
; comparison against the 1981 cartridge.
;
; Stock's nine bytes at $F1B9-$F1C1 become a jump and six of unreachable
; filler; $F1C2 is `LF1C2`, a branch target, so the patch stops short of it and
; this returns there by name.
TNVAR:
        ldy     $80
        iny
        lda     TNENT
        and     #TNE_NET
        beq     TNVAR1          ; not in a match: stock's step of one
        iny                     ; in a match: two, so the parity holds
TNVAR1: cpy     #$04
        bcc     TNVAR2
        ldy     #$00            ; stock wraps to 0...
        lda     TNENT
        and     #TNE_NET
        beq     TNVAR2
        ldy     #$01            ; ...and a match wraps to 1, because 0 is the
                                ;   computer and this is the whole point
TNVAR2: jmp     TNVARX
