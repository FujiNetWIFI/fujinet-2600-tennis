; tndefs.inc -- zero page, banks and constants for networked Tennis.
;
; EQUATES ONLY. Included BEFORE the ORG, like fujinet.inc. The code half is
; tncore.inc and goes inside it.

; ---------------------------------------------------------------------------
; ZERO PAGE
;
; This console has 128 bytes of RAM. Walking every addressing mode in
; $F000-$F7FF over the verified disassembly -- and bounding each index by hand,
; which is the only way to do it -- the highest cell Tennis names is $D5, as
; the second operand of `STA $D3,X` with X in 0..1 and then plainly at $F201's
; `STX $D5`. That leaves $D6-$FF: forty-two cells, against Dragster's
; thirty-four and Combat's twenty-six.
;
; The stack's measured maximum depth is FIVE bytes, so six are kept for it.

; ---------------- the netcode, $D6-$E5, always live ----------------
TNENT   EQU     $D6             ; entry mode, role, phase, and the flags below
TNSEQ   EQU     $D7             ; the sequence we are waiting for
TNERR   EQU     $D8             ; low nibble last transport error, high nibble
                                ;   consecutive stalled ticks -- the watchdog
                                ;   needs no cell of its own
TNTICK  EQU     $D9             ; sim tick, low 8 bits
TNNST   EQU     $DA             ; the network machine's state
TNCRCV  EQU     $DB             ; the sim checksum, SAMPLED AT A TICK BOUNDARY
                                ;   and always of the state ENTERING TNTICK.
                                ;   It cannot be sampled where it is sent: the
                                ;   transport free-runs and Tennis's logic runs
                                ;   every frame, so "the state" changes four
                                ;   times a tick and two consoles reading it
                                ;   whenever their transport got round to it
                                ;   disagree for no reason at all.
TNADV   EQU     $DC             ; nonzero: the sim advances this frame. Written
                                ;   by TNSHIM at $F051, in the kernel bank;
                                ;   read by the stall gate in the game bank,
                                ;   later in the same frame.
TNTMP   EQU     $DD             ; scratch, dead between routines
TNWARM  EQU     $DE             ; bit 7: the game bank's cold path has run.
                                ;
                                ; ITS OWN CELL, AND NOT A BIT OF TNENT, for a
                                ; reason that is pure arithmetic: the bank is
                                ; entered at $1000 and the clear's re-entry
                                ; LF004 is at $1004, so the dispatcher between
                                ; them is FOUR BYTES. `bit TNWARM / bmi` is
                                ; exactly four. Testing a bit of TNENT would
                                ; cost `lda/and/bne` -- seven -- and TNENT's
                                ; bits 6 and 7, the only two BIT can reach
                                ; without a mask, are the tick phase.
TNCLRX  EQU     $DF             ; the clear's seed. Stock passes it in X --
                                ;   $00 cold, $85 on RESET, $88 on SELECT --
                                ;   and two of those three still do. The cold
                                ;   one cannot, because TNGOTO leaves X = $FF,
                                ;   so the cold path recognises itself by
                                ;   TNWARM and forces $00. See tnstart.inc.
TNSWA   EQU     $E0             ; the synthetic SWCHA  -- patch at $F203
TNSWB   EQU     $E1             ; the synthetic SWCHB  -- patches at $F02B,
                                ;   $F17C and $F1F9
TNTRIG  EQU     $E2             ; 2: triggers BY PORT, TNTRIG,X -- patch at
                                ;   $F376. Zero page in the RIOT where INPT4 is
                                ;   zero page in the TIA, so that patch is the
                                ;   same opcode and the same four cycles.
TNW0    EQU     $E4             ; port 0's wire byte, during the shim
TNW1    EQU     $E5             ; port 1's

; ---------------- the union, $E6-$F9 ----------------
; The rings belong to a match; the transport's setup cells and the input
; scanner belong to the boot bank, which finishes before a match can start. The
; only thing that could make them overlap in time is a cold start from inside a
; match, which empties everything anyway.
;
; Game side:
TNRWAT  EQU     $E6             ; highest remote tick received
TNRING  EQU     $E7             ; 8: remote input ring, index = tick AND 7
TNLOC   EQU     $EF             ; 4: local input ring, index = tick AND 3
TNRDN   EQU     $F3             ; records left to consume; also the window
                                ;   cursor, as (TNRDN-1)*IR_LEN
                                ; $F4-$F9 spare
; Session side:
FNDEV   EQU     $E6             ; transaction setup, read by FNBEG
FNCMD   EQU     $E7
FNNPR   EQU     $E8
FNTMO   EQU     $E9             ; timeout in ~0.6s quanta
FNCNT   EQU     $EA             ; timeout quanta remaining
FNPTRL  EQU     $EB             ; reply-window cursor
FNPTRH  EQU     $EC
FNPCNT  EQU     $ED             ; bytes written into the current path payload
INCUR   EQU     $EE             ; input scan, both halves
INPREV  EQU     $EF
CSDLY   EQU     $F0             ; a frame counter for the session's wait loops.
                                ;   IN MEMORY, NOT IN A REGISTER: the text
                                ;   kernel counts scanlines in X and Y and
                                ;   clobbers both, and a caller that counted its
                                ;   own repeats in one of them ran its wait 256
                                ;   times over.
; Shared by both, because it means the same thing to both:
FNSEQ   EQU     TNSEQ

; THE REMOTE RING IS EIGHT, AND THE EIGHT IS LOAD-BEARING. Console B can only
; advance to tick T_B with A's input for T_B, which A sent at its own tick
; T_B - d. So T_B <= T_A + d, and A has sent up to T_A + d <= T_B + 2d: B can
; be holding records for T_B .. T_B + 2d, which is 2d+1 slots. At d = 2 that is
; five, and eight is the next power of two, for a free AND #7 instead of a
; modulo. A four-slot ring silently overwrites a tick that has not been used
; yet, and the symptom is the peer's player occasionally repeating a move.

; ---------------- the bound on all THREE clears ----------------
; Tennis has ONE clear loop and THREE ways into it, which is Dragster's trap
; with an extra entry. `LF004` is re-entered by `JMP LF004` from $F1AB with X
; preloaded: $85 on RESET and $88 on SELECT, and $00 on the cold path. The loop
; runs X up to $FF and `STY VSYNC,X` wraps inside page zero, so stock clears
; the whole of RAM and strobes the entire TIA register file on the way past.
;
; A restage happens on every RESET and every SELECT -- several times a minute
; while somebody is picking a game -- so unbounded it would wipe every cell
; above on a press, not on some rare path.
;
; The loop is bounded at TNZPLO and all three entries share the bound. The cold
; entry still covers $00-$D5, which contains all of $00-$7F, so the TIA strobe
; that clear is really for is untouched.
TNZPLO  EQU     $D6             ; the first cell the clear may not touch
TNSTKLO EQU     $FA             ; the stack. Measured deepest: 5 bytes.

; ---------------------------------------------------------------------------
; THE TICK
;
; K = 4 video frames a tick, 15 ticks a second. Four and not the three the
; latency probe allows, because a power of two makes the ring index a free AND.
; The probe measures a full WRITE/STATUS/READ cycle at three frames, so the
; transport keeps up with a four-frame tick with a frame to spare.
;
; The phase is two bits of TNENT and NOT a count in a cell the game can reach.
; All three of Tennis's clear entries run through $00-$D5, and a sim clock the
; game resets on a frame of its own choosing re-phases the tick underneath the
; transport.
TNK     EQU     4               ; frames per tick
TNKM    EQU     TNK-1           ; ...as a mask, which is why it is a power of two

; d, the input delay, in ticks. Hardcoded rather than taken from START's delay
; byte: it sizes the local ring at assembly time, and a console that honoured a
; delay its ring could not hold would overwrite a tick it had not used yet.
; The server is asked for this value and START is checked against it.
TND     EQU     2               ; 2 ticks = 8 frames = ~133 ms, symmetric
TNLM    EQU     3               ; local ring mask; the ring is 4, and 4 > d

; THERE IS NO DISPLAY LEAD, and that is a decision rather than an omission.
;
; Dragster draws its countdown tree K*d frames early, because the tree is a
; deterministic function of the simulation and can therefore honestly be shown
; before the simulation reaches it -- which matters there because the elapsed
; time IS the game.
;
; Tennis has no quantity of that kind. Where the ball goes next is a function
; of what the players do next, and nothing can show that early. Both players
; are delayed identically, so neither is ahead of the other; what it costs is
; that the ball is about 133 ms harder to reach than it is on one console, and
; the README says so rather than implying otherwise.

; TNENT is SEVERAL INDEPENDENT BITS, and keeping them independent matters.
;
;   bit 1  NETWORKED. Set by the boot bank when a match has started.
;
; The WARM bit that would naturally live here is in TNWARM instead, and the
; reason is the four-byte dispatcher -- see TNWARM above. They were one value
; in a sibling port -- 0 cold, 1 local, 2 playing -- and the boot bank handing
; over with 2 meant the game bank read "not cold" and jumped straight into the
; frame loop, so the game's own initialisation never ran and both consoles came
; up identically wrong. Identically, which is why the transport looked perfect.
TNE_NET EQU     $02             ; in a match
TNE_ROLE EQU    $04             ; set = guest, and it is PORT 1
TNE_RAN EQU     $08             ; set = the last boundary ran its tick, so the
                                ;   next one must advance TNTICK before it
                                ;   samples anything
TNE_LEFT EQU    $10             ; the peer left: the boot bank says so in words
TNE_RSY EQU     $20             ; a checksum mismatch was seen: press RESET
TNE_PH  EQU     $C0             ; 2 bits: which frame of the tick this is

; ---------------------------------------------------------------------------
; THE WIRE
;
; One byte carries a player's whole console, which is what lets the ring be a
; ring. The senses are the hardware's, straight through, so the mixer is masks
; and shifts and never an inversion -- which matters more than it sounds,
; because an inversion applied on one console and not the other is a desync
; that only shows when somebody presses something.
IW_TRIG EQU     $80             ; 1 = released      (INPT4's sense)
IW_DIFF EQU     $40             ; 1 = A / Pro       (SWCHB's sense)
IW_SEL  EQU     $20             ; 1 = not pressed
IW_RST  EQU     $10             ; 1 = not pressed
IW_STICK EQU    $0F             ; 1 = not pushed    (SWCHA's sense)

; IW_DIFF IS LIVE ON THIS CONSOLE, unlike Dragster's, and it carries each
; player's OWN switch. $F1F9 masks SWCHB with LF7FE,X -- $40 for port 0 and $80
; for port 1 -- into $D3,X, and $F3BD reads it to clamp the return angle that
; player's racket can produce. It reaches play, so it crosses the wire, which is
; Combat's rule and Video Olympics'.
IW_IDLE EQU     IW_TRIG|IW_DIFF|IW_SEL|IW_RST|IW_STICK

; Both rings are SEEDED with IW_IDLE rather than with zero, and the difference
; is not cosmetic: zero reads as EVERY switch pressed and the stick shoved in
; all four directions, because the wire keeps the hardware's active-low sense.
;
; The seeding is what makes the first d ticks honest. A record is stamped
; TNTICK+d, so the first one anybody sends is for tick d and the slots for ticks
; 0..d-1 are never sent by anyone. Both consoles filling them with the same
; constant is the only way the two agree about a tick neither has heard anything
; about; filling them with a LOCAL capture would be asymmetric.

; A record is six bytes of payload, eight on the wire with len and type.
IR_TICK EQU     0               ; the tick the window ENDS at
IR_IN   EQU     1               ; 3 bytes: that tick and the two before it
IR_CRC  EQU     4               ; the boundary-sampled checksum
IR_CHK  EQU     5               ; the XOR check byte
IR_LEN  EQU     8               ; ON THE WIRE, and a power of two on purpose
IR_MASK EQU     $F8
IR_SEED EQU     $A5             ; the check byte's salt

TNWIN   EQU     3               ; ticks of input in every record, newest first.
                                ; THE TRANSPORT IS NOT LOCKED TO THE TICK: a
                                ; WRITE/STATUS/READ cycle that runs long means a
                                ; tick's record is never sent at all, and the
                                ; peer's watermark tracks only the newest tick
                                ; received, so the hole leaves the PREVIOUS
                                ; LAP's byte in that ring slot and the gate uses
                                ; it. Three covers any gap the local ring can
                                ; still supply, which is one less than its four.

; ---------------------------------------------------------------------------
; THE NETWORK MICRO-STEP MACHINE
;
; Stepped from inside the wait for the one timed band to run out. Tennis has
; exactly one, and it is the good shape:
;
;   $F1A2  arm    TIM64T = $2D = 2880 cycles, at the end of the kernel
;   $F051  spin   LDA INTIM / BNE, at the top of the NEXT frame
;
; Between them lie RESET/SELECT, the whole game logic and the per-frame display
; setup. Measured worst case is 2438 cycles used, so 442 remain on the frame a
; point is scored; the median frame leaves 2175.
;
; There is no overscan timer to fall back on. Tennis's overscan is a hard-coded
; 24-line WSYNC spin at $F15F throwing away about 1630 cycles, and arming
; TIM64T at $F149 would convert it into a second timed window at no visual
; cost. That is held in reserve for the day `make slack` says the first window
; is not enough.
;
; The loop re-reads INTIM before every step and stops when the slack is gone, so
; THE HOOK CANNOT OVERRUN THE KERNEL BY CONSTRUCTION -- there is no budget to
; get wrong. That matters more here than in the sibling ports: this spin polls
; every 7 cycles and 7 is coprime with 256, so a missed zero is not a late exit
; but a wait of up to another 1800 cycles -- 23 scanlines, and a rolling
; picture. TIM64T decrements every 64 cycles, so a gate of 8 guarantees at least
; 7 x 64 = 448 cycles remain, against a worst step of about 310.
TNGATE  EQU     8

NS_IDLE EQU     0               ; no socket, or no match: do nothing
NS_WISS EQU     2               ; issue the WRITE and launch it
NS_WACK EQU     4               ; wait for the cartridge to acknowledge
NS_SISS EQU     6               ; issue the STATUS
NS_SACK EQU     8               ; ...and read how many bytes are waiting
NS_RISS EQU     10              ; issue the READ
NS_RACK EQU     12              ; ...and turn its length into a record count
NS_CONS EQU     14              ; consume one record from the reply window
NS_FAIL EQU     16              ; count it and start the cycle again
NS_LAST EQU     NS_FAIL         ; the dispatch table's last entry

; ---------------------------------------------------------------------------
; BANKS
;
; An image is (N+1) x 2048 -- N banks then the fixed half -- and MAME's cart
; slot accepts only N in {1,3,7,15}. Three banks, and the fourth slot is the
; fixed half.
BANKBOOT EQU    0               ; cold start, the session, the WAITING screen
BANKGAME EQU    1               ; the frame logic, the scoring, and the shim
BANKKERN EQU    2               ; the display half, and the transport
NBANKS   EQU    3

; The trampoline, at a fixed address in the fixed tail because the banks are
; assembled separately. A bank switch is a JUMP, never a call: the store that
; switches is the last instruction fetched from the old bank, so there is no
; return address to come back to. "Where to carry on" is DATA, in TNWARM.
TNGOTO  EQU     FNTAIL

; THE TWO SEAMS, AND WHY THEY COST NO PATCHED BYTES.
;
; Every byte keeps its cartridge address, so the bank that does NOT own a seam
; address has a hole there, and the trampoline goes in the hole:
;
;   A: game -> display, at $F015. TWO ways in and ONE trampoline serves both:
;      `JMP LF015` at $F494, every frame, and the cold path falling out of
;      `JSR LF5E7` at $F012. Both are stock and unpatched. In the game bank
;      $1015 is a hole, and the trampoline lives there.
;
;   B: display -> game, at $F168, a fall-through out of the 24-line tail spin.
;      In the kernel bank $1168 is a hole, and the other trampoline sits in it.
;
; SEAM B IS AT $F168 AND NOT $F16C ON PURPOSE. `LDX #$03` at $F168 feeds the
; three `STX WSYNC/VSYNC/VBLANK` stores that follow, and TNGOTO clobbers X;
; putting the load on the game side of the seam means nothing has to survive
; the switch at all. Y does survive and must -- the spin leaves Y = 0 and
; $F17B's `DEY` turns it into $FF -- and TNGOTO never touches Y.
;
; The switch costs about 35 cycles and they are free: the very next instruction
; is `STA WSYNC`. The spin exits 7 cycles into a scanline, so the store lands 42
; cycles in and still waits out the same line. `make frames` checks that rather
; than trusting it.
TNSEAMA EQU     $1015           ; the game bank's trampoline to the kernel
TNSEAMB EQU     $1168           ; the kernel bank's trampoline to the game

; Two addresses DiStella never labelled, because nothing branches to them.
; Spelling them here means a typo is a build error rather than a jump into the
; middle of an instruction -- tools/patches.py asserts the opcode at each.
TNSPIN  EQU     $1051           ; LDA INTIM, the spin that ends the timed band
TNRESUM EQU     $1168           ; LDX #$03, where the game bank resumes

; ---------------------------------------------------------------------------
; THE WIRE PROTOCOL
;
; frame := len(1) type(1) payload(len-1). `len` counts type and payload, so a
; frame is never zero-length and the reader can reject a bad one on sight.
WT_HELO EQU     $01
WT_LOBBY EQU    $03
WT_JOIN EQU     $04
WT_START EQU    $05
WT_INPUT EQU    $06
WT_CRC  EQU     $07
WT_STATE EQU    $08
WT_RESYN EQU    $09
WT_BYE  EQU     $0A
WT_LEFT EQU     $0B
WT_PING EQU     $0C
WT_PONG EQU     $0D

WPROTO  EQU     2               ; HELLO's version byte, carrying the television
                                ; standard between the version and the name.

TVNTSC  EQU     0
TVPAL   EQU     1

; ---------------------------------------------------------------------------
; APPKEYS
;
; Two of them, both under the family's shared creator and app:
;
;   key 0            the FujiNet USERNAME, which every game in the family reads
;   key TNAPPKEY     the room's own URL, which the Lobby writes there on the way
;                    out and the booted game reads on the way in. There is no
;                    HTTP "join a room" call anywhere in this family; joining is
;                    a local write and a reboot.
;
; Both are read into a CARTRIDGE PATH BUFFER and never into console RAM. That is
; not thrift, it is necessity: a name is eight bytes and a URL is sixty-four.
TNAPPKEY EQU    26              ; Tennis's room. Combat 24, Dragster 25, and
                                ;   Video Olympics is still on 24 pending
                                ;   provisioning on the firmware wiki registry.
AKNAMEK EQU     0
PB_NAME EQU     FP_SEL0         ; path buffer 0: the username
PB_URL  EQU     FP_SEL1         ; path buffer 1: the server URL
NAMEMIN EQU     2
NAMEMAX EQU     8
