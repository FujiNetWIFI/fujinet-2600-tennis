; tnnet.inc -- the match: the lockstep boundary and the transport.
;
; NOT WRITTEN YET, and deliberately loud about it rather than quietly absent.
;
; Milestone 3 is the shim alone. Every console port read has become a read of a
; RAM shadow, and `make det` has to prove that a build which fills those
; shadows from its own console still plays exactly like the 1981 cartridge --
; because only then is a later difference the network's fault and nothing
; else's.
;
; Two entry points are referenced from tninput.inc and both are stubs:
;
;   TNNET    the tick boundary: advance TNTICK, sample the checksum, capture
;            this console's stick, and decide whether the peer's input for this
;            tick has arrived. Until it exists, a build that somehow reached it
;            would still be a Tennis cartridge, so it falls back to the local
;            path rather than to nothing.
;
;   TNNSTEP  one bounded micro-step of the WRITE/STATUS/READ cycle. The loop in
;            tninput.inc is already shaped around it -- it re-reads INTIM before
;            every call and stops when the slack is gone -- and it never runs
;            until TNE_NET is set, which nothing sets yet.
TNNET:
        jmp     TNLOC0

TNNSTEP:
        rts
