; tnnet.inc -- the non-blocking network machine, in the game bank.
;
; Battleship's NPGO blocks a whole transaction and spends each wait redrawing a
; frame. That is right for a turn-based game polling every ninety frames and
; fatal here: the sim has to keep running while the transport works.
;
; So the transport is a state machine of bounded micro-steps, run from a loop
; inside the wait for Tennis's OVERSCAN timer to expire, which re-reads INTIM
; before every step. The machine free-runs: every cycle it sends this console's
; record for tick+d and consumes whatever has arrived. It is never TOLD that a
; tick has passed and never has to be -- a duplicate record is idempotent,
; because the ring is indexed by tick, and a machine running slower than the
; tick rate simply causes a stall, which is exactly the right behaviour.
;
; It lives in the GAME bank, which is where both the room and the hook are.
; Tennis's split leaves the game bank 1204 free bytes against the kernel
; bank's 766, and the hook at $F285 is in the game bank too -- so unlike
; Combat, where the machine and the loop that steps it lived in the kernel bank
; and the shim lived in the other, everything here is one bank and TNWAIT can
; simply JSR the shim.
;
; The mailbox at $1D00-$1FFF is fixed and visible from every bank, so the
; netcode would work from either; it should run where the room is.

; ---------------------------------------------------------------------------
; TNNSTEP -- one bounded step.
;
; Dispatched through the RTS trick, which is 26 cycles flat. A chain of CMP/BEQ
; would be sixty at worst and, worse, VARIABLE -- and the gate's arithmetic
; above is stated in terms of a worst case, so a constant one is worth having.
TNNSTEP:
        ldx     TNNST
        lda     TNJT+1,x
        pha
        lda     TNJT,x
        pha
        rts

; The table holds addr-1, because RTS increments what it pulls.
TNJT:
        DW      TNSIDLE-1
        DW      TNSWISS-1
        DW      TNSWACK-1
        DW      TNSSISS-1
        DW      TNSSACK-1
        DW      TNSRISS-1
        DW      TNSRACK-1
        DW      TNSCONS-1
        DW      TNSFAIL-1

TNSIDLE:
        rts

; ---------------------------------------------------------------------------
; TNLNCH -- commit a transaction and go to state A.
;
; THE NEXT SEQUENCE NUMBER COMES FROM THE CARTRIDGE'S OWN PERSISTED ACKSEQ + 1,
; never a counter in RAM. This connector has no reset line: the RESET switch
; restarts the 6507 and leaves the cartridge running, so a program-local counter
; would restart at 1, collide with a sequence already answered, and the
; transaction would be dropped in silence.
TNLNCH:
        sta     TNNST
        lda     FNACKS
        clc
        adc     #1
        bne     TNLN1
        lda     #1              ; 0 is reserved as "never used"
TNLN1:  sta     TNSEQ
        ldx     #FR_SEQ
        jmp     FNRW            ; the single commit that launches it

; TNACK -- has the cartridge answered? Returns with Z set if it has.
;
; ACKSEQ is published LAST, after the reply, its length and the error code are
; all in place, so a match means everything behind it is already readable.
TNACK:
        lda     FNACKS
        cmp     TNSEQ
        rts

; ---------------------------------------------------------------------------
; WRITE -- our record for tick + d.
TNSWISS:
        lda     #NETDEV
        ldx     #FR_DEV
        jsr     FNRW
        lda     #NCWRITE
        ldx     #FR_CMD
        jsr     FNRW
        lda     #1
        ldx     #FR_NPAR
        jsr     FNRW
        lda     #0
        ldx     #FR_DRST
        jsr     FNRW
; ONE parameter of TWO bytes -- the length -- and not two of one. The stream is
; NPARAM x { size byte, then that many little-endian value bytes }, so a length
; sent as two one-byte parameters is a different request entirely.
        lda     #2
        sta     FNTX
        lda     #IR_LEN
        sta     FNTX
        lda     #0
        sta     FNTX
; The frame: len, type, then six bytes of payload. Eight on the wire, and eight
; is a power of two on purpose -- see the note on TNSSACK.
        lda     #IR_LEN-1
        sta     FNTX
        lda     #WT_INPUT
        sta     FNTX
; b0, the tick the window ENDS at: d ticks ahead of the one being simulated.
        lda     TNTICK
        clc
        adc     #TND
        sta     FNTX
        sta     TNW0            ; TEMP1, borrowed within this step only
        eor     #IR_SEED
        sta     TNW1            ; the check byte, accumulated as the frame goes
;
; b1, b2, b3 -- A WINDOW OF THREE TICKS, NEWEST FIRST, NOT ONE BYTE.
;
; THE TRANSPORT IS NOT LOCKED TO THE TICK AND CANNOT BE. It free-runs a
; WRITE/STATUS/READ cycle and sends whatever tick the sim is on when the WRITE
; comes round; a cycle that happens to take longer than one tick -- a burst of
; records to consume is enough, at one record a step -- means the sim advances
; twice between two WRITEs and A TICK'S RECORD IS NEVER SENT AT ALL. You cannot
; send N ticks in fewer than N writes, so the answer is not to try: batch.
;
; The hole this left was invisible for as long as nobody touched anything. The
; peer's ring is indexed by tick and its watermark tracks only the NEWEST tick
; received, so a missing tick in the middle left the previous lap's byte in that
; slot and the gate happily used it. In attract mode every byte on the wire is
; IDLE, so a stale IDLE and a fresh one are the same byte -- sixty thousand
; checksums agreed. The instant somebody pressed RESET the two consoles ran
; different code on the same frame: one restarted the game and the other did
; not, and they were a frame of simulation apart for ever after.
;
; Three is enough and is not arbitrary. The local ring is four slots, so ticks
; t, t-1 and t-2 are all still live. A gap wider than that would need the peer
; to be more than three ticks ahead of our transport, and lockstep prevents it:
; if our records stop, the peer stalls, which stops the peer's records, which
; stalls US -- and a stalled sim does not advance TNTICK while the transport
; keeps running, so the transport catches up. It is self-correcting in exactly
; the direction that matters.
        ldx     #TNWIN
TNWW:   lda     TNW0
        and     #TNLM
        tay
        lda     TNLOC,y
        sta     FNTX
        eor     TNW1
        sta     TNW1
        dec     TNW0            ; RMW on zero-page RAM, which is allowed; the
        dex                     ;   ban is on the cartridge's write-only pages
        bne     TNWW
; b4, the checksum SAMPLED AT THE LAST TICK BOUNDARY -- not computed here.
; See the note on TNCRCV: the transport free-runs and the sim moves every
; frame, so a checksum taken at this instant is a different quantity on the two
; consoles and disagrees for no reason.
        lda     TNCRCV
        sta     FNTX
        eor     TNW1
        sta     FNTX            ; b5, the check byte, over the whole frame
        lda     #NS_WACK
        jmp     TNLNCH

TNSWACK:
        jsr     TNACK
        beq     TNWA1
        rts
TNWA1:  lda     FNERR
        bne     TNFJ1
        lda     #NS_SISS
        sta     TNNST
        rts

; A trampoline per state, because TNSFAIL is further than a branch reaches from
; most of them. Each one sits immediately after an rts, so the straight line
; through the state above cannot run into it -- which is the mistake that made
; the latency probe report a failure carrying the step id of the step that had
; just succeeded.
TNFJ1:  jmp     TNSFAIL

; ---------------------------------------------------------------------------
; STATUS -- how many bytes are waiting.
;
; STATUS BEFORE READ IS NOT OPTIONAL, and the reason is worse than the family
; states it: NParser::read resizes its buffer to the length asked for and then
; copies that many bytes out of a receive buffer that may be shorter. That is
; not an error path, it is undefined behaviour returning plausible garbage.
; Never ask for more than the last STATUS reported.
TNSSISS:
        lda     #NETDEV
        ldx     #FR_DEV
        jsr     FNRW
        lda     #NCSTAT
        ldx     #FR_CMD
        jsr     FNRW
        lda     #2
        ldx     #FR_NPAR
        jsr     FNRW
        lda     #0
        ldx     #FR_DRST
        jsr     FNRW

; THE PARAMETERS ARE (1, 0) AND THEY GO THROUGH FNPB, which emits each one as
; a SIZE byte and then its value. Written raw, the four bytes `01 00 01 00`
; look like the family's "two one-byte params, 1,0,1,0" -- and they are not:
; they are two parameters whose values are BOTH ZERO, because the 1s in that
; phrase are the size bytes FNPB supplies.
;
; NET STATUS refuses unit 0. fujinet-pc answers `cmd:53 dlen:-1`, the reply
; length is never valid, no record is ever read, the peer's input never
; arrives, and after fifteen stalled ticks the console decides the peer has
; gone and goes back to the boot bank to reconnect. Two consoles doing that to
; each other is a match that restarts every four seconds with ZERO CRC
; MISMATCHES in the relay log -- because everything that did run, ran in
; lockstep. The session's own STATUS had been right all along, eight lines of
; `jsr FNPB` away in tnsess.inc, and `dlen:4` in the same log next to
; `dlen:-1` is what made the two comparable.
        lda     #1
        jsr     FNPB
        lda     #0
        jsr     FNPB
        lda     #NS_SACK
        jmp     TNLNCH

TNSSACK:
        jsr     TNACK
        beq     TNSA1
        rts
TNSA1:  lda     FNERR
        bne     TNFJ2
; The fourth status byte is nDevStatus_t and SUCCESS is 1, not 0. It is the
; only place a transport error is visible: a dead socket reports a perfectly
; plausible zero bytes waiting.
        lda     FNRPLY+NSDEVST
        cmp     #1
        bne     TNFJ2
; THE SETTLE RULE, AND WHY IT IS NOT THE FAMILY'S. Battleship polls until two
; consecutive readings agree, because an HTTP body arrives in pieces and its
; length is not known in advance. Here every frame is exactly eight bytes, so
; reading `avail AND $F8` reads only WHOLE frames and a partial one is simply
; left for next time. The misframing the two-readings rule guards against is
; structurally unreachable, and the second STATUS round trip it would cost is a
; third of a tick. This is a deliberate departure and not an oversight.
        lda     FNRPLY+NSAVHI
        bne     TNSA2           ; 256 or more waiting: take a capped bite
        lda     FNRPLY+NSAVLO
        and     #IR_MASK
        beq     TNSA3           ; nothing whole yet
        sta     TNRDN
        lda     #NS_RISS
        sta     TNNST
        rts
TNSA2:  lda     #$F8            ; 248 bytes, 31 records, well inside the window
        sta     TNRDN
        lda     #NS_RISS
        sta     TNNST
        rts
TNSA3:  lda     #NS_WISS        ; nothing to read: send the next record
        sta     TNNST
        rts

TNFJ2:  jmp     TNSFAIL

; ---------------------------------------------------------------------------
; READ -- exactly the whole frames STATUS reported.
TNSRISS:
        lda     #NETDEV
        ldx     #FR_DEV
        jsr     FNRW
        lda     #NCREAD
        ldx     #FR_CMD
        jsr     FNRW
        lda     #1
        ldx     #FR_NPAR
        jsr     FNRW
        lda     #0
        ldx     #FR_DRST
        jsr     FNRW
        lda     #2
        sta     FNTX
        lda     TNRDN
        sta     FNTX
        lda     #0
        sta     FNTX
        lda     #NS_RACK
        jmp     TNLNCH

TNSRACK:
        jsr     TNACK
        beq     TNRA1
        rts
TNRA1:  lda     FNERR
        bne     TNFJ3
; CAPTURE THE LENGTH NOW. FNRXLO belongs to the most recent transaction and the
; next one replaces it -- and the next one is always zero, because a WRITE
; returns nothing.
        lda     FNRXLO
        lsr     a
        lsr     a
        lsr     a               ; bytes to records; the frame size is 8
        sta     TNRDN
        lda     #NS_CONS
        sta     TNNST
        rts

TNFJ3:  jmp     TNSFAIL

; ---------------------------------------------------------------------------
; CONSUME -- one record a step, backwards.
;
; Backwards so that TNRDN is both the counter and the cursor: the record at
; index n starts at n*8. Order does not matter, because every record carries its
; own tick and writes its own ring slot.
;
; The records are read IN PLACE out of the cartridge's reply window. Nothing is
; copied to console RAM but the one byte that goes in the ring, because there is
; no RAM to copy into: the 128 bytes are Tennis's.
TNSCONS:
        lda     TNRDN
        bne     TNCO1
        lda     #NS_WISS
        sta     TNNST
        rts
TNCO1:  dec     TNRDN
        lda     TNRDN
        asl     a
        asl     a
        asl     a
        tay
        lda     FNRPLY+1,y      ; the type
        cmp     #WT_INPUT
        beq     TNCO2
        cmp     #WT_LEFT
        bne     TNCO9
; The peer left. Saturate the stall watchdog as well as recording the reason:
; the gate switches to the peer-left screen when that counter tops out, and
; setting it here is what makes a CLEAN leave instant instead of making the
; player wait out the same second an unplugged console would cost.
        lda     #$F0|FNEPEER
        sta     TNERR
        rts
TNCO2:
; The check byte, over every byte of the frame that carries anything. With
; whole-frame reads a misframe cannot happen, so this can only ever catch a
; genuinely corrupt record -- which is the point: it is the difference between
; "the peer's tank moved oddly" and a counter that says so.
        lda     FNRPLY+2,y
        eor     FNRPLY+3,y
        eor     FNRPLY+4,y
        eor     FNRPLY+5,y
        eor     FNRPLY+6,y
        eor     #IR_SEED
        cmp     FNRPLY+7,y
        bne     TNCO9           ; a bad record is dropped, never acted on
;
; THE WHOLE WINDOW GOES IN, not just its newest byte -- that is the entire point
; of sending one. Each of the three bytes is written to the slot for ITS OWN
; tick, so a record that never arrived is filled in by the next one or the one
; after that, and the hole never reaches the gate.
;
; Re-writing a slot is safe and is not a violation of "a record, once sent, is
; immutable": the value being written is the peer's capture for that tick, which
; is fixed the moment it is captured, so a second write puts the same byte back.
        lda     FNRPLY+2,y      ; the tick this window ends at
        and     #7
        tax
        lda     FNRPLY+3,y
        sta     TNRING,x
        lda     FNRPLY+2,y
        sec
        sbc     #1
        and     #7
        tax
        lda     FNRPLY+4,y
        sta     TNRING,x
        lda     FNRPLY+2,y
        sec
        sbc     #2
        and     #7
        tax
        lda     FNRPLY+5,y
        sta     TNRING,x
; ---------------------------------------------------------------------------
; DESYNC DETECTION, ON THE CONSOLE ITSELF.
;
; The relay has always reported mismatches, but a log on another machine cannot
; repair anything. The record carries the sender's checksum, sampled at ITS last
; tick boundary -- which is the boundary of tick (b2 - d), because the record is
; stamped d ticks ahead. So when that tick is the one WE are on, the peer's
; checksum and our TNCRCV are samples of the same instant and must be equal.
;
; Only then. TNCRCV is a single cell holding the current tick's sample and there
; is no room for a history of them, so a record whose tick does not line up is
; simply not compared. That costs nothing: a desync is persistent, and the ticks
; line up within a few of them.
;
; A false positive is impossible -- a difference here IS a difference in the
; simulation -- and a corrupt record was already dropped by the check byte.
        lda     FNRPLY+2,y
        sec
        sbc     #TND
        cmp     TNTICK
        bne     TNCO8           ; not comparable this time; there will be more
        lda     TNENT
        and     #TNE_RSY
        bne     TNCO8           ; already armed. Records from before the repair
                                ;   are still in flight and must not arm it
                                ;   again on their way past.
        lda     FNRPLY+6,y
        cmp     TNCRCV
        beq     TNCO8
        lda     TNENT
        ora     #TNE_RSY        ; TNCAP presses RESET on the wire; see cbcap.inc
        sta     TNENT
TNCO8:
; Advance the watermark only if this record is NEWER. Records can arrive in any
; order after a catch-up burst, and a stale one must not pull the watermark
; back. Signed, like the gate's compare, and for the same reason: the tick is
; eight bits and wraps.
        lda     FNRPLY+2,y
        sec
        sbc     TNRWAT
        bmi     TNCO9
        lda     FNRPLY+2,y
        sta     TNRWAT
TNCO9:  rts

; ---------------------------------------------------------------------------
TNSFAIL:
        lda     TNERR
        and     #$F0
        ora     #FNEBAD
        sta     TNERR
        lda     #NS_WISS
        sta     TNNST
        rts
