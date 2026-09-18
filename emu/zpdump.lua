-- zpdump.lua -- the whole of RAM at the det sampling point, for one frame in N.
-- Forensic only: `make det` names a frame, this says which byte.
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local frame = 0
local FROM = tonumber(os.getenv("DUMP_FROM") or "") or 1
local N    = tonumber(os.getenv("DUMP_N") or "") or 4
_G._zp = sp:install_write_tap(0x0296, 0x0296, "tim64t", function()
    frame = frame + 1
    if frame < FROM or frame >= FROM + N then return end
    local t = {}
    for a = 0x80, 0xFF do t[#t+1] = string.format("%02X", sp:readv_u8(a)) end
    print(string.format("ZP %d %s", frame, table.concat(t, " ")))
end)
