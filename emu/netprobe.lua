-- netprobe.lua -- the lockstep cells, once a second, on one console.
-- Forensic. `make rig` says the pair disagreed; this says what either of them
-- thought was happening.
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local TNENT, TNERR, TNTICK, TNNST, TNCRCV = 0xD6, 0xD8, 0xD9, 0xDA, 0xDB
local TNRWAT, TNRDN = 0xE6, 0xF3
local n = 0
_G._np = sp:install_write_tap(0x0296, 0x0296, "tim64t", function()
    n = n + 1
    if n % 60 ~= 0 then return end
    print(string.format(
        "NET f=%-5d ent=$%02X err=$%02X tick=$%02X rwat=$%02X nst=%-2d crc=$%02X rdn=$%02X",
        n, sp:readv_u8(TNENT), sp:readv_u8(TNERR), sp:readv_u8(TNTICK),
        sp:readv_u8(TNRWAT), sp:readv_u8(TNNST), sp:readv_u8(TNCRCV),
        sp:readv_u8(TNRDN)))
end)

-- Who sends us back to bank 0, and from where? TNGONE is the netcode giving up
-- on the peer; TNCOLD is the 6507 having taken its reset vector.
local cpu = manager.machine.devices[":maincpu"]
_G._np_bank = sp:install_write_tap(0x1D80, 0x1D80, "bank0", function()
    print(string.format("TOBOOT %.2fs pc~$%04X ent=$%02X err=$%02X tick=$%02X rwat=$%02X",
        manager.machine.time:as_double(), cpu.state["PC"].value,
        sp:readv_u8(0xD6), sp:readv_u8(0xD8), sp:readv_u8(0xD9),
        sp:readv_u8(0xE6)))
end)

-- When does this console actually enter a match? The handover is the store
-- that sets TNE_NET.
_G._np_ent = sp:install_write_tap(0xD6, 0xD6, "tnent", function(off, data, mask)
    if (data & 0x02) ~= 0 and (sp:readv_u8(0xD6) & 0x02) == 0 then
        print(string.format("ENTER %.2fs ent=$%02X",
            manager.machine.time:as_double(), data))
    end
end)
