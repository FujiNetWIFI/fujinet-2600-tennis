; tnmix.inc -- the tick boundary, and two wire bytes into the three cells
; Tennis reads.

; ---------------------------------------------------------------------------
; TNNET -- in a match, once a frame, from TNSHIM.
TNNET:
; Only a tick boundary has anything to decide; the other three frames of a tick
; re-use the shadows that are already there.
        lda     TNENT
        and     #TNE_PH
        beq     TNNT1
        jsr     TNPHI           ; mid-tick: bump the phase
        rts                     ; ...and the sim runs: TNADV is already $FF

TNNT1:
; A RECORD, ONCE SENT, IS IMMUTABLE. Everything below happens ONLY on a boundary
; that actually advanced the tick.
;
; The transport free-runs: it sends the record for TNTICK+d whenever it reaches
; its WRITE step, several times per tick, and a duplicate is harmless only
; because it is IDENTICAL. Re-capturing on a stalled boundary makes it not
; identical -- the peer has already stored and SIMULATED the first version of
; tick X while this console goes on to use the second.
;
; It is invisible while nobody touches anything, because every capture returns
; the same idle byte. The moment somebody presses SELECT during a stall, one
; console counts the press and the other does not -- and in Tennis that means
; two consoles on different game variations, one of them playing the computer.
        lda     TNENT
        and     #TNE_RAN
        beq     TNNT1B          ; the tick has not moved: nothing to re-commit

        inc     TNTICK
        lda     TNENT
        and     #$FF-TNE_RAN
        sta     TNENT

; The checksum, of the state ENTERING the tick just advanced to. The boundary is
; the one instant two consoles can agree on.
        jsr     TNCRC
        sta     TNCRCV

; And this console's own stick, for the tick d ahead. Captured once, here, and
; never revised.
        jsr     TNCAP
        pha
        lda     TNTICK
        clc
        adc     #TND
        and     #TNLM
        tay
        pla
        sta     TNLOC,y
TNNT1B:

; Has the peer's input for this tick arrived? TNRWAT is the highest remote tick
; received.
;
; THIS IS THE ONE SIGNED COMPARE IN THE NETCODE, and it is signed on purpose:
; the tick is eight bits and wraps, and a window of +-127 either side is what
; makes the wrap invisible.
        lda     TNRWAT
        sec
        sbc     TNTICK
        bpl     TNNT2
        jmp     TNSTALLQ

TNNT2:
; ---- the tick runs ----
        lda     TNERR           ; a tick that ran clears the stall counter
        and     #$0F
        sta     TNERR

; Our own byte for this tick was captured d ticks ago and has been sitting in
; the local ring ever since. That is the whole of delay-based lockstep: our
; input is delayed exactly as far as the peer's, so both players feel the same
; lag and neither is ahead of the other.
        lda     TNTICK
        and     #TNLM
        tay
        lda     TNLOC,y
        sta     TNW0

; The peer's byte for the same tick, out of the remote ring.
        lda     TNTICK
        and     #7
        tay
        lda     TNRING,y
        sta     TNW1

; The host is PORT 0 -- the left port's nibble of SWCHA, INPT4, $D3, $C5, $C7.
; A guest's own byte is therefore port 1's and the two have to change places.
;
; IT IS THE PORT AND NOT THE COURT. Tennis has two index spaces: the per-player
; loop counts a COURT index in $89, and identity -- difficulty, trigger, points,
; games -- is indexed by $89 EOR $D0. $D0 flips every game, which is how the
; players change ends while their scores follow them. The wire is in port space
; and the game's own EOR does the rest; a role keyed off the court index would
; swap the two players every time they changed ends.
        lda     TNENT
        and     #TNE_ROLE
        beq     TNMIX
        lda     TNW0
        ldy     TNW1
        sty     TNW0
        sta     TNW1

; ---------------------------------------------------------------------------
; TNMIX -- two wire bytes into the three cells Tennis reads.
;
; Every bit keeps the hardware's sense on the wire, so this is masks and shifts
; and never an inversion. An inversion applied on one console and not the other
; is a desync that only shows when somebody presses something.
TNMIX:
; SWCHA. Tennis reads it ONCE, at $F203, and picks a nibble with `CPX $D0 /
; BEQ` -- the high nibble for the player whose court index equals $D0, the low
; nibble for the other. The high nibble is the console's LEFT port, which is
; port 0, so that is where the host's byte goes.
        lda     TNW0
        and     #IW_STICK
        asl     a
        asl     a
        asl     a
        asl     a
        sta     TNTMP
        lda     TNW1
        and     #IW_STICK
        ora     TNTMP
        sta     TNSWA

; The triggers: bit 7 of each byte, straight through, in the pair Tennis
; indexes with the PORT number at $F376.
        lda     TNW0
        and     #IW_TRIG
        sta     TNTRIG
        lda     TNW1
        and     #IW_TRIG
        sta     TNTRIG+1

; SWCHB, and it carries three different kinds of bit.
;
; RESET and SELECT are ANDed: both are active low, so the AND means EITHER
; PLAYER MAY PRESS THEM -- which is the couch semantics, preserved. There is no
; host authority here and none is needed: every bit is a pure function of the
; two wire bytes, so both consoles compute the same byte without a protocol
; for it.
        lda     TNW0
        and     TNW1
        lsr     a               ; IW_RST is bit 4 and SWCHB's reset is bit 0,
        lsr     a               ;   IW_SEL is bit 5 and select is bit 1
        lsr     a
        lsr     a
        and     #$03
        sta     TNTMP

; THE DIFFICULTY SWITCHES CROSS THE WIRE, one per player, and unlike Dragster's
; they are not inert. $F1F9 masks SWCHB with LF7FE,X -- $40 for port 0 and $80
; for port 1 -- into $D3,X, and $F3BD reads that to clamp the range of return
; angles that player's racket can produce. It reaches play, so it travels.
;
; Each console captures its own LEFT difficulty switch into IW_DIFF, because
; both players sit at their own console with the stick in port 1. Port 0's
; lands on SWCHB bit 6 where it already is; port 1's shifts up to bit 7.
        lda     TNW0
        and     #IW_DIFF
        ora     TNTMP
        sta     TNTMP
        lda     TNW1
        and     #IW_DIFF
        asl     a               ; $40 -> $80
        ora     TNTMP
        sta     TNTMP

; B&W STAYS LOCAL, and it is provably safe: Tennis reads SWCHB bit 3 at $F02B
; and again at $F17C, and both reach only the six colour cells $BB-$C0 and the
; attract masks $82/$83. No colour cell reaches physics, and none is
; checksummed. So each player sees their own set in whichever they chose.
        lda     SWCHB
        and     #$08
        ora     TNTMP
        ora     #$34            ; bits 2, 4 and 5 read high on a real console
        sta     TNSWB

; Mark the tick as run -- the next boundary does the increment, immediately
; before it samples -- and bump the phase, because this frame's logic will run.
        lda     TNENT
        ora     #TNE_RAN
        sta     TNENT
        jmp     TNPHI

; ---------------------------------------------------------------------------
; TNPHI -- advance the frame-in-tick phase.
;
; FOUR BYTES OF WORK. The phase is the top two bits of TNENT, so adding $40
; increments it and the carry out of bit 7 is simply discarded -- the low bits,
; which are the mode, role and flag bits, cannot be disturbed because nothing
; carries into them. Masking would be three instructions of ceremony.
;
; Called on every frame whose logic will run, and on NO stalled frame: a stall
; leaves the phase where it is so the next frame is the same boundary again,
; which is exactly what retrying a tick means.
;
; THE PHASE IS NOT A CELL THE GAME CAN REACH. All three of Tennis's clear
; entries sweep $00-$D5, and a sim clock the game resets on a frame of its own
; choosing re-phases the tick underneath the transport. Re-phasing is not a
; desync by itself -- both machines re-phase at the same simulated frame -- but
; the moment anything makes one of them advance a frame earlier, the clock they
; use to measure the disagreement is itself derived from the disagreement.
TNPHI:  lda     TNENT
        clc
        adc     #$40
        sta     TNENT
        rts

; The stall, reached by a jump because the branch above cannot carry that far.
TNSTALLQ:
        jmp     TNSTALLN
