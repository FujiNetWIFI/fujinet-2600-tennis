-- wsync.lua -- forensic: how many WSYNC strobes per frame, and from where.
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local n, frames, hist, sites = 0, 0, {}, {}
_G._w1 = sp:install_write_tap(0x0002, 0x0002, "wsync", function()
    n = n + 1
    local pc = manager.machine.devices[":maincpu"].state["PC"].value
    sites[pc] = (sites[pc] or 0) + 1
end)
_G._w2 = sp:install_write_tap(0x0296, 0x0296, "tim64t", function()
    frames = frames + 1
    if frames > 3 then hist[n] = (hist[n] or 0) + 1 end
    n = 0
end)
_G._w3 = emu.add_machine_stop_notifier(function()
    local ks = {}
    for k in pairs(hist) do ks[#ks+1] = k end
    table.sort(ks)
    for _, k in ipairs(ks) do print(string.format("WSYNC/frame %d : %d frames", k, hist[k])) end
    local ps = {}
    for k in pairs(sites) do ps[#ps+1] = k end
    table.sort(ps)
    for _, k in ipairs(ps) do print(string.format("SITE $%04X x%d", k, sites[k])) end
end)
