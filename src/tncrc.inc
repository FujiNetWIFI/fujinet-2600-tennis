; tncrc.inc -- the sim checksum, the desync repair, and the peer-left exit.

; ---------------------------------------------------------------------------
; TNCRC -- a checksum of everything the two consoles must agree about.
;
; Sampled at the tick boundary and NOWHERE ELSE, always of the state ENTERING
; TNTICK. It cannot be sampled where it is sent: the transport free-runs and
; Tennis's logic runs on every frame, so "the state" moves four times a tick and
; two consoles reading it whenever their transport got round to it disagree for
; no reason at all.
;
; WHAT IT COVERS IS THE DESIGN OF IT, and the first three cells are not the ones
; that move.
;
;   $80  the variation, 0-3
;   $81  = $80 AND 1: 0 = one player against the computer, 1 = two humans
;   $D0  the end swap, which flips every game
;
; Two consoles on different variations draw visibly different games while
; agreeing perfectly about every coordinate, for as long as it takes something
; to move the ball apart -- and two that disagree about $D0 have each player
; standing at the wrong end. A checksum must cover every cell that SELECTS
; behaviour, not only the cells behaviour writes.
;
; Then the simulation proper. Tennis's ball is genuinely three-dimensional --
; depth, horizontal and HEIGHT, three 16-bit fixed-point axes integrated at
; $F40C with gravity as a 16-bit subtract -- so the fractions are in here too.
; A difference in a fraction byte is a difference that becomes visible four
; frames later and would otherwise be reported as a desync with no cause.
;
; WHAT IT MUST NOT COVER (Video Olympics 3.15): the shadows TNSWA/TNSWB/TNTRIG,
; TNADV, and anything else the netcode itself fills. A cell the netcode writes
; is not state the netcode should check. Nor $BB-$C0, the six colours: they are
; rebuilt every frame from the LOCAL black-and-white switch, which is exactly
; why that switch can stay local.
;
; Nor $8A. It is scratch, and it holds the byte Tennis's score kernel reads one
; past the end of its font every frame -- see emu/det.lua. Both consoles read
; the same one, `make glyph` says so, and it is not simulation state.
;
; Rotate-and-add rather than a plain sum: a plain sum cannot see two bytes that
; swapped, and two players that swapped ends is exactly the failure this is for.
; THE TABLE IS INDEXED WITH Y AND THE CELL WITH X, and that is forced rather
; than chosen: the 6502 has `LDX abs,Y` and `ADC zp,X` and neither of their
; mirrors. `ADC zp,Y` does not exist, and an assembler that quietly accepted it
; would have been the more interesting problem.
TNCRC:
        lda     #0
        sta     TNTMP
        ldy     #TNCRCN-1
TNCRC1: ldx     TNCRCT,y        ; the cell's address
        lda     TNTMP
        asl     a
        adc     #0              ; ASL puts bit 7 in carry, ADC puts it in bit 0
        clc
        adc     $00,x
        sta     TNTMP
        dey
        bpl     TNCRC1
        lda     TNTMP
        rts

; The cells, as a table rather than as unrolled code: eighteen bytes of ROM
; against fifty-four of LDA/ADC, and the list is then something a person can
; read and argue with.
TNCRCT:
        DB      $80, $81, $D0           ; what SELECTS behaviour
        DB      $8C, $8D, $8E           ; the ball's three fractions
        DB      $8F, $90, $91           ; depth, horizontal, HEIGHT
        DB      $92, $95                ; depth velocity, hi and lo
        DB      $93, $96                ; horizontal velocity
        DB      $94, $97                ; height velocity -- gravity acts here
        DB      $98, $99                ; both players' depth
        DB      $9A, $9B                ; both players' horizontal
        DB      $A0                     ; serve pending
        DB      $B4                     ; who struck the ball last
        DB      $C5, $C6                ; points in this game
        DB      $C7, $C8                ; games won in this set
        DB      $CA                     ; who is serving
        DB      $CB                     ; the freeze/match state, which gates
                                        ;   the whole logic chain
        DB      $CC                     ; the bounce count
        DB      $D1                     ; DEUCE / ADVANTAGE message mode
        DB      $D2                     ; the set and tiebreak counter
        DB      $84                     ; the frame counter -- BEHIND THE GATE
                                        ;   now, and it selects which frames run
                                        ;   in variations 2 and 3
TNCRCN  EQU     *-TNCRCT

; ---------------------------------------------------------------------------
; TNRSY -- the desync repair, pressed into this console's own capture byte.
;
; Entered from TNCAP with the finished wire byte in A.
;
; NOTICING IS NEARLY FREE. Every record carries the sender's checksum, sampled
; at its own last tick boundary -- which is the boundary of tick b-d, since the
; record is stamped d ticks ahead. So when that tick is the one WE are on, the
; peer's checksum and our TNCRCV are samples of the same instant and must be
; equal. A record whose tick does not line up is simply not compared, which
; costs nothing: a desync is persistent, and the ticks line up within a few of
; them. A false positive is impossible -- a difference here IS a difference in
; the simulation -- and a corrupt record was already dropped by the check byte.
;
; REPAIRING IS A SYNTHETIC RESET, and it reaches both ends by the road every
; real press takes: ANDed on the wire and delivered through the delay ring, so
; both machines restart on exactly the same tick. A repair that happened at
; different ticks on the two consoles would just be a second desync, and there
; is no spare zero page to store a scheduled tick in. There did not need to be.
;
; IT IS TOTAL HERE, unlike Combat's. Combat could only restart the round because
; tank X lives in the TIA's HMP0/HMP1 and is applied incrementally by HMOVE, so
; nothing in RAM could carry it. Tennis's player and ball positions are all in
; RAM and the RESET path clears $85-$FF and re-runs LF5E7, which rewrites the
; whole init block into $90-$B3 -- so every checksummed cell above $84 is
; rebuilt from constants. What is left is $80, $81, $84 and $D0, and all four
; agree by construction: the variation moves only on SELECT presses that are
; ANDed on the wire, the frame counter counts simulated frames, and $D0 is
; rewritten by LF5E7 as well.
;
; The scores go with it, and that is correct rather than a wart: the two
; consoles have just disagreed, so their scores may have too, and the only
; state agreed by construction is the state the game builds from nothing.
;
; ONE-SHOT. The bit clears as it presses, so the press lasts one tick. One
; console arming it is enough, for the same reason either player may start a
; game -- and both usually do arm, because each one sees the other's checksum.
TNRSY:
        pha                     ; the finished wire byte, kept
        lda     TNENT
        and     #TNE_RSY
        beq     TNRSY9
        lda     TNENT
        and     #$FF-TNE_RSY    ; one-shot: clear it as we press
        sta     TNENT
        pla
        and     #$FF-IW_RST     ; RESET pressed, in the hardware's active-low
        rts                     ;   sense, straight onto the wire
TNRSY9: pla
        rts

; ---------------------------------------------------------------------------
; TNGONE -- the peer is gone.
;
; Two causes and one result. A clean leave arrives as a PEER_LEFT frame and the
; transport saturates the stall counter on the spot; a console that was
; unplugged saturates it by simply not arriving, fifteen stalled ticks at four
; frames each being about a second.
;
; It hands back to the boot bank, which is the only bank with a text kernel, and
; says so in words. TNE_NET goes off and TNWARM with it, so the game bank will
; take its cold path if it is ever entered again.
TNGONE:
        lda     TNENT
        and     #$FF-TNE_NET
        ora     #TNE_LEFT
        sta     TNENT
        lda     #0
        sta     TNWARM
        lda     #BANKBOOT
        jmp     TNGOTO
