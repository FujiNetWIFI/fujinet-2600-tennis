; tncap.inc -- this console's own half of the stick, and the non-networked path.

; ---------------------------------------------------------------------------
; TNLOC0 -- not in a match: the console drives both players, exactly as it
; always did.
;
; This path is byte-for-byte the behaviour `make det` compares against stock,
; and it is why the shim went in before the network did: a build that played
; differently here would make every later difference ambiguous.
;
; It is a plain mirror rather than a reconstruction, because Tennis reads its
; ports whole and splits them itself. $F203 takes SWCHA and picks a nibble with
; `CPX $D0 / BEQ`; $F1F9 takes SWCHB and masks it per player with `AND LF7FE,X`;
; $F376 indexes the trigger pair by port. Nothing here has to know which player
; is which -- that is the game's business, and in a match it becomes TNMIX's.
;
; THE TRIGGERS ARE STORED RAW. $F376 tests bit 7 with `BMI` and stock handed it
; the untouched port value, so anything cleverer here would be a difference for
; nothing.
TNLOC0:
        lda     SWCHA
        sta     TNSWA
        lda     SWCHB
        sta     TNSWB
        lda     INPT4
        sta     TNTRIG
        lda     INPT5
        sta     TNTRIG+1
        rts

; ---------------------------------------------------------------------------
; TNCAP -- this console's own console, packed into one wire byte.
;
; THE LEFT PORT, ON BOTH CONSOLES, WHATEVER THE ROLE.
;
; The role decides which PLAYER this byte drives, and that is settled by the
; swap in TNNET, where it belongs. Here there is no role at all: each player
; sits at their own console with one joystick in port 1, and a guest who had to
; plug into port 2 to move their own player would be right about nothing except
; the bit numbering. It is the kind of mistake that is invisible in a rig, where
; nobody's hands are on anything, and obvious the moment two people play.
TNCAP:
        lda     SWCHA           ; bits 7-4: the LEFT port, joystick 1
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     TNTMP
        lda     INPT4           ; and its trigger
        and     #IW_TRIG        ; bit 7, straight through
        ora     TNTMP
        sta     TNTMP

; RESET and SELECT, SHIFTED UP FROM BITS 0 AND 1 TO BITS 4 AND 5.
;
; That shift is the whole of a bug worth remembering. SWCHB puts RESET at bit 0
; and SELECT at bit 1; the wire byte puts them at 4 and 5, because 0-3 are the
; stick. Masking SWCHB with #$30 and calling it done reads the console's UNUSED
; bits 4 and 5 instead -- which read high on real hardware and in MAME, so the
; wire says "not pressed" for ever and NEITHER SWITCH EVER REACHES THE OTHER
; CONSOLE. The mixer at the far end shifts them back down correctly, so both
; halves look right in isolation and the pair is silently broken.
;
; It survives every gate that only checks agreement: two consoles that both
; ignore SELECT agree perfectly.
        lda     SWCHB
        and     #$03            ; bit 0 RESET, bit 1 SELECT, both active low
        asl     a
        asl     a
        asl     a
        asl     a               ; -> IW_RST and IW_SEL
        ora     TNTMP
        sta     TNTMP

; THIS PLAYER'S OWN DIFFICULTY SWITCH, and it is the LEFT one on both consoles
; for the same reason the stick is the left port: each player is sitting at
; their own machine. It lands on SWCHB bit 6 for the host and is shifted to
; bit 7 for the guest by TNMIX, which is where the role is known.
;
; Unlike Dragster's, this bit is not carried for the sake of a tidy idle value.
; Tennis reads it: $F1F9 masks it per player into $D3,X and $F3BD clamps the
; racket's return angle with it.
        lda     SWCHB
        and     #IW_DIFF        ; bit 6, the LEFT difficulty switch
        ora     TNTMP

; The desync repair is a synthetic RESET pressed into this byte.
        jmp     TNRSY

; ---------------------------------------------------------------------------
; TNSTALLN -- the peer's input for this tick has not arrived.
;
; Nothing else happens. The frame still runs: VSYNC still goes out, the band's
; timer still runs, and the kernel still draws. What does not happen is the
; game logic, so the picture freezes coherently and the display never shudders.
;
; The phase is NOT bumped here. A stall leaves it where it is so the next frame
; is the same boundary again, which is exactly what retrying a tick means. The
; frame counters are not ticked either, which is what keeps $84 a count of
; simulated frames on both consoles however differently they stalled.
;
; The stall count lives in TNERR's high nibble, saturating. A stall is normal;
; fifteen in a row at four frames each is a second, and that is a peer that has
; gone. Saturation is the ONE "the peer is gone" condition: a clean leave
; arrives as a PEER_LEFT frame and the transport saturates this on the spot, and
; a console that was unplugged saturates it by simply not arriving.
TNSTALLN:
        lda     #0
        sta     TNADV
        lda     TNERR
        cmp     #$F0            ; the low nibble is an error code below $10, so
        bcc     TNSTA1          ;   this is exactly "high nibble is already $F"
        jmp     TNGONE
TNSTA1: clc
        adc     #$10            ; +1 in the high nibble, low nibble untouched
        sta     TNERR
        rts
