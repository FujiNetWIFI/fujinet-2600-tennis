# The cartridge goes here

One file, which is not in this repository:

| File | What | md5 |
|---|---|---|
| `tennis.bin` | the 2048-byte Tennis cartridge dump | `42cdd6a9e42a3639e190722b8ea3fc51` |

**Why it is not here.** Tennis is Activision's, published 1981 and written by
David Crane. This repository is a patch and a server. It is not a place to
redistribute the cartridge, so you bring your own.

**The disassembly is not here either, and for a different reason.** No
commented disassembly of Tennis exists, in this tree or anywhere else, so this
one is *generated*:

```
make disasm        # distella -paf -c tools/tennis.cfg rom/tennis.bin > rom/tennis.asm
```

`make verify-org` then proves two things on every build, and only the second is
a round trip:

1. **the code/data split is the one a recursive descent finds.**
   `tools/checkmap.py` walks from the reset vector and requires the regions
   `tools/tennis.cfg` declares to match exactly — every CODE byte reached,
   every DATA byte not. This is not a formality: a run of data bytes
   disassembled as instructions re-encodes to exactly the bytes it came from,
   so claim 2 below cannot see a wrong split at all.
2. and the result assembles to `rom/tennis.bin` **byte for byte**.

Tennis is unusually well behaved here and the checker says so rather than
assuming it: 857 instructions, 1609 code bytes, and the remaining 439 in one
contiguous run at `$F649-$F7FF`. There is no `JMP (ind)` in the image and no
RTS-dispatch, so there is no computed control flow for an automatic pass to
lose — which is what forced the sibling port to hand-steer its map.

`rom/tennis.asm` is never edited. Every change to the program is declared in
`tools/patches.py` and applied at build time, so there is no hand-maintained
copy of Tennis in this tree for a patch to drift away from.

Without the dump, `make verify-org`, `make tennis` and everything downstream
will not run. Nothing else in the repository needs it.
