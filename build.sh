#!/usr/bin/env bash
# build.sh -- assemble the Atari 2600 networked Tennis client.
#
#   ./build.sh verify-org   the anti-drift gate: rom/tennis.asm regenerated
#                           from the dump, its code/data split proved by
#                           recursive descent, converted, assembled, and
#                           required to be rom/tennis.bin byte for byte
#   ./build.sh probe        the M1 transaction-latency probe, flat 4K
#   ./build.sh              the client
#
# The client is (N+1) x 2048 bytes: N 2K banks then the 2K fixed half. MAME's
# vcs_cart_slot_device::call_load() accepts only 4096/8192/16384/32768, so N is
# 1, 3, 7 or 15 and nothing between. Tennis needs three banks.
#
# The "FUJI" claim is stamped into the fixed half. Without it the cartridge
# treats the image as an ordinary game and the mailbox goes dead the moment it
# boots.
#
# Env:
#   FUJI_FIRMWARE=...  firmware tree (default ~/Workspace/fn-2600)
#   TVSTD=ntsc|pal     the television standard, baked in (default ntsc)
#   DISTELLA=...       the disassembler (default ~/Workspace/distella/distella)

set -euo pipefail
cd "$(dirname "$0")"

FUJI_FIRMWARE="${FUJI_FIRMWARE:-$HOME/Workspace/fn-2600}"
VCS="$FUJI_FIRMWARE/pico/atari-2600"
DISTELLA="${DISTELLA:-$HOME/Workspace/distella/distella}"

# ---------------------------------------------------------------------------
# THE TELEVISION STANDARD, AND THE ONE IMAGE THIS WILL NOT BUILD.
#
# A 2600 cannot measure which television it is plugged into. The ROM generates
# the video timing itself; there is no register, no interrupt and no external
# reference to read. That is why every 2600 game in history shipped as separate
# NTSC, PAL and SECAM images rather than detecting the standard at runtime, and
# it is why the refusal has to live HERE, at the only moment anyone knows.
#
# THE REASON IS DRAGSTER'S, NOT COMBAT'S, and the difference is worth keeping
# straight. Combat and Video Olympics refuse SECAM because their two players
# are told apart by LUMINANCE, which a SECAM TIA ignores entirely -- so the two
# sprites collapse to one colour and no amount of lockstep helps. That is not
# Tennis's situation: its two players take their colour from $BD and $BE, which
# the table at $F751 fills with $4C and $8C -- hue 4 and hue 8, two different
# hues, which a SECAM set renders as two different colours.
#
# It is refused anyway, for the duller and more honest reason: this port has no
# SECAM colour table. $F751 is six bytes of hue-luminance and $F757 six more
# for black-and-white, and authoring a SECAM set of them means somebody looking
# at the result on a real SECAM television. Neither has happened.
case "${TVSTD:-ntsc}" in
    ntsc) tvstd=0 ;;
    pal)  tvstd=1 ;;
    secam)
        cat >&2 <<'SECAM'
build.sh: refusing to build a SECAM image.

  NOT for Combat's reason. Tennis tells its two players apart by HUE -- $BD and
  $BE are loaded from $F751 with $4C and $8C, hue 4 and hue 8 -- and a SECAM
  TIA, which picks its eight colours by the hue nibble, would render those as
  two different colours perfectly well.

  The reason is that this port has no SECAM colour table. $F751 is six bytes of
  hue-luminance and $F757 is six more for black-and-white; a SECAM set of them
  has to be authored, and somebody has to look at the result on a real SECAM
  television. Neither has happened, and shipping an unlooked-at palette is
  worse than shipping nothing.

  This is refused at build time because a 2600 cannot detect its own television
  standard at runtime: the ROM generates the video timing and there is nothing
  to read. The relay refuses a console that declares SECAM as well, and the two
  refusals have to agree.

  TVSTD=ntsc (the default) or TVSTD=pal.
SECAM
        exit 1 ;;
    *)
        echo "build.sh: TVSTD must be ntsc, pal or secam (got '${TVSTD}')" >&2
        exit 1 ;;
esac

if command -v asl >/dev/null 2>&1; then
    AS=asl P2BIN=p2bin
elif [ -x "$HOME/asl/asl" ]; then
    AS="$HOME/asl/asl" P2BIN="$HOME/asl/p2bin"
else
    echo "build.sh: no Macroassembler AS found (tried PATH and ~/asl)" >&2
    exit 1
fi

mkdir -p build
HERE=$(pwd)

# The "FUJI" claim is stamped post-link because its file offset depends on the
# image size: the fixed half is the LAST 2K, so the offset is
# (size - 2048) + (FN_R_CLAIM - $1800).
stampclaim() {
    local f=$1 size
    size=$(stat -c%s "$f")
    printf 'FUJI' | dd of="$f" bs=1 \
        seek=$((size - 0x800 + 0x0710)) conv=notrunc status=none
    echo "$f: $size bytes"
}

# AS writes its .p and .lst next to the source, so assemble from the source's
# own directory and collect the artefacts into build/.
assemble() {  # assemble <basename> [srcdir]
    local b=$1 d=${2:-src}
    ( cd "$d" && "$AS" -q -L -i . -i "$HERE/src" -i "$HERE/build" "$b.asm" )
    if [ "$d" != "build" ]; then
        mv "$d/$b.p" "build/$b.p"
        mv -f "$d/$b.lst" "build/$b.lst" 2>/dev/null || true
    fi
}

# The disassembly is GENERATED, never committed and never hand-edited. Two
# claims have to hold and only the second is a round trip:
#
#   checkmap.py  the declared code/data split is the one a recursive descent
#                from the reset vector actually finds. A run of data bytes
#                disassembled as instructions re-encodes to exactly the bytes
#                it came from, so the round trip below cannot see a wrong split
#                at all -- it is the failure mode Video Olympics' PORTING.md
#                warns about, and this is the gate for it.
#   cmp          and then it really is the cartridge, byte for byte.
disasm() {
    [ -x "$DISTELLA" ] || {
        echo "build.sh: no DiStella at $DISTELLA (set DISTELLA=...)" >&2
        exit 1
    }
    "$DISTELLA" -paf -c tools/tennis.cfg rom/tennis.bin > rom/tennis.asm
    echo "disasm: $(wc -l < rom/tennis.asm) lines"
}

if [ "${1:-}" = "disasm" ]; then
    disasm
    exit 0
fi

# ---------------- M0: the conversion gate ----------------
if [ "${1:-}" = "verify-org" ]; then
    [ -f rom/tennis.asm ] || disasm
    python3 tools/checkmap.py rom/tennis.bin tools/tennis.cfg
    python3 tools/dasm2as.py rom/tennis.asm > build/tn_org.asm
    assemble tn_org build
    "$P2BIN" build/tn_org.p build/tn_org.bin -r '$F000-$F7FF' -l 255 -q
    rm -f build/tn_org.p
    cmp build/tn_org.bin rom/tennis.bin
    echo "verify-org: byte-identical ($(stat -c%s build/tn_org.bin) bytes)"
    exit 0
fi

# ---------------- M1: the transaction latency probe ----------------
#
# A flat 4K image: one bank of code and the fixed half. The endpoint is
# regenerated every run so a stale value cannot survive an environment change,
# and it is the whole reason this is a build-time string rather than something
# read from an appkey -- the probe has to run before there is a lobby.
if [ "${1:-}" = "probe" ]; then
    {
        echo "; generated by build.sh -- do not edit"
        printf 'UENDPT: DB      "%s"\n' \
            "${ENDPOINT:-N:TCP://127.0.0.1:9605/}"
        echo "        DB      0"
    } > build/endpoint.inc
    printf '; generated by build.sh\nCSHNLEN EQU 0\n' > build/playername.inc

    assemble probe
    python3 tools/checkbanks.py build/probe.lst $((0x1800)) probe
    "$P2BIN" build/probe.p build/probe.bin -r '$1000-$1FFF' -l 255 -q
    rm -f build/probe.p
    stampclaim build/probe.bin
    python3 "$VCS/tools/checkrom.py" build/probe.bin
    exit 0
fi

# ---------------- the client: three banks and the fixed half ----------------
BANKS="tnboot tngame tnkern"

# Build-time switches, regenerated every run so a stale value cannot survive an
# environment change. They must be EQUates and not IFDEFs: AS resolves IF in its
# FIRST PASS, and a condition naming a symbol defined further down the file is
# not a build error -- it quietly takes the branch it should not.
{
    echo "; generated by build.sh -- do not edit"
    printf 'TNLAG   EQU     %s\n' "${TNLAG:-0}"
    printf 'TNTVSTD EQU     %s\n' "$tvstd"
} > build/cfg.inc

{
    echo "; generated by build.sh -- do not edit"
    printf 'UENDPT: DB      "%s"\n' "${ENDPOINT:-TCP://127.0.0.1:9602/}"
    echo "        DB      0"
} > build/endpoint.inc
{
    name="${PLAYER:-PLAYER1}"
    echo "; generated by build.sh -- do not edit"
    printf 'CSHNLEN EQU     %d\n' $(( ${#name} ))
    printf '        DB      "%s"\n' "$name"
} > build/playername.inc

[ -f rom/tennis.asm ] || disasm

# tennis.inc is GENERATED from the pristine disassembly on every build, through
# the same converter verify-org proves faithful and the declared patch map in
# tools/patches.py. There is no hand-maintained copy of the game in this tree
# for a patch to drift away from.
python3 tools/dasm2as.py rom/tennis.asm > build/tn_org.asm
assemble tn_org build
python3 tools/mkbanks.py build/tn_org.lst rom/tennis.asm build/tennis.inc

# The baseline for the patch audit: the same pristine source, at the address a
# cartridge bank is actually mapped at. See tools/check_patch.py.
python3 tools/dasm2as.py --window rom/tennis.asm > build/tn_win.asm
assemble tn_win build
"$P2BIN" build/tn_win.p build/tn_win.bin -r '$1000-$17FF' -l 255 -q
rm -f build/tn_win.p

# THE TAIL IS ASSEMBLED FIRST: it holds the shared transport, and the banks
# reach it through build/tail.inc, generated from the addresses the tail
# actually assembled to. Nothing keeps a list of those by hand.
assemble tntail
python3 tools/mktail.py build/tntail.lst \
    FNRW,FNARM,FNCHK,FNBEG,FNPB,FNPW,FNGO,FNACK,\
FNROWA,FNCHR,FNENDR > build/tail.inc
tail -1 build/tail.inc | sed 's/^; */  tail: /'

parts=()
for b in $BANKS; do
    assemble "$b"
    python3 tools/checkbanks.py "build/$b.lst" $((0x1800)) "$b"
    "$P2BIN" "build/$b.p" "build/$b.bin" -r '$1000-$17FF' -l 255 -q
    rm -f "build/$b.p"
    parts+=("build/$b.bin")
done

"$P2BIN" build/tntail.p build/tntail.bin -r '$1800-$1FFF' -l 255 -q
rm -f build/tntail.p

cat "${parts[@]}" build/tntail.bin > build/tennis.bin
rm -f "${parts[@]}" build/tntail.bin

stampclaim build/tennis.bin
python3 tools/checkrom_filter.py "$VCS/tools/checkrom.py" build/tennis.bin
python3 tools/check_zp.py build/tngame.lst
python3 tools/check_patch.py build/tn_win.bin build/tennis.bin
