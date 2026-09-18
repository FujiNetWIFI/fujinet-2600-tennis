-- glyph.lua -- the score kernel's out-of-bounds read must be STABLE.
--
-- emu/det.lua explains the read: $F527's `LDA ($B2),Y` runs once per frame
-- with Y = $FF and lands at $F8(B2-1), which is past the end of a 2K cartridge.
-- Stock mirrors it back into the display kernel; the FujiNet cartridge puts the
-- mailbox there instead.
--
-- Fidelity is det.lua's business and it is bounded and documented. THIS is the
-- half that lockstep actually needs, and it is a different claim: whatever the
-- byte is, it has to be the SAME on both consoles and it has to STAY the same.
-- A text plane that still held the words one console drew during the handshake
-- -- "PLAYER ONE" on the host and "PLAYER TWO" on the guest -- would put a
-- different byte under each console's racket-angle clamp, and the two would
-- diverge on a rally rather than on anything the netcode did.
--
-- So: $8A is sampled every frame at the same point det.lua samples, and the
-- run has to report exactly one distinct value -- and, since TNFCNT now writes
-- it from the generated fold, that value has to be the fold. GLYPH_WANT is
-- passed in by the Makefile out of build/mirror.inc, so the two cannot drift.
--
--   ./run.sh tennis glyph
local WANT = tonumber(os.getenv("GLYPH_WANT") or "") -- nil on stock: any
                                                     -- constant will do there
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local seen, n, frames = {}, 0, 0

_G._gl = sp:install_write_tap(0x0296, 0x0296, "tim64t", function()
    frames = frames + 1
    if frames < 30 then return end          -- let the cold path finish
    local v = sp:readv_u8(0x8A)
    if seen[v] == nil then seen[v] = 0; n = n + 1 end
    seen[v] = seen[v] + 1
end)

_G._gl_end = emu.add_machine_stop_notifier(function()
    local keys = {}
    for k in pairs(seen) do keys[#keys + 1] = k end
    table.sort(keys)
    local parts = {}
    for _, k in ipairs(keys) do
        parts[#parts + 1] = string.format("$%02X x%d", k, seen[k])
    end
    print(string.format("GLYPH %d frames, %d distinct value(s): %s",
                        frames, n, table.concat(parts, ", ")))
    if n ~= 1 then
        print("GLYPH FAIL -- it moves, so two consoles can disagree about it")
    elseif WANT and seen[WANT] == nil then
        print(string.format("GLYPH FAIL -- constant, but not the fold $%02X", WANT))
    else
        print("GLYPH PASS -- the out-of-bounds read is the constant stock folds to")
    end
end)
