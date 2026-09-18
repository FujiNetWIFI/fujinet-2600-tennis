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
