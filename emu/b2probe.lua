local sp = manager.machine.devices[":maincpu"].spaces["program"]
local seen, n, frames = {}, 0, 0
_G._p = sp:install_write_tap(0x0296,0x0296,"t",function()
    frames = frames + 1
    if frames < 20 then return end
    local b2, b3, a8 = sp:readv_u8(0xB2), sp:readv_u8(0xB3), sp:readv_u8(0x8A)
    local k = string.format("$B3:$B2=%02X%02X -> $8A=%02X", b3, b2, a8)
    if seen[k] == nil then seen[k]=0; n=n+1 end
    seen[k] = seen[k]+1
end)
_G._e = emu.add_machine_stop_notifier(function()
    local ks={} for k in pairs(seen) do ks[#ks+1]=k end table.sort(ks)
    for _,k in ipairs(ks) do print(string.format("B2 %s x%d", k, seen[k])) end
    print("B2 distinct "..n)
end)
