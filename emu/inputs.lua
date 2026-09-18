-- inputs.lua -- every read of the console's input ports, with the PC that did it.
--
-- The disassembly says Tennis's whole input surface is five reads; this is what
-- turns that from a claim into a measurement, and it is how the patch map is
-- proved COMPLETE rather than merely plausible.
--
-- WHAT A PASS LOOKS LIKE HERE is simpler than in either sibling, because
-- Tennis has no analog input to leave behind:
--
--   * locally, EVERY read comes from TNLOC0 -- one SWCHA, one SWCHB, one
--     INPT4 and one INPT5, once a frame each, and the game's own five sites
--     are gone;
--   * in a match, every read comes from TNCAP instead, and TNLOC0 never runs.
--
-- Video Olympics had to keep two kernel reads, because a paddle is sampled by
-- counting scanlines until its capacitor charges and that capture is how a
-- console learns its own position. A joystick has no such story: the shim can
-- own every read there is.
--
-- THE MIRRORS MATTER. The TIA decodes only A0-A3 for a read, so INPT4 answers
-- at $0C, $1C, $2C, $3C and so on. Tennis reaches it at the $0C base -- its
-- `LDY $0C,X` is what DiStella spells `LDY REFP1,X`, because $0C is REFP1 on a
-- WRITE -- but a tap placed on one mirror alone would report no trigger reads
-- at all, which reads exactly like a game that does not use the button.
--
--   ./run.sh tennis inputs

dofile(os.getenv("A2600_EMU") .. "/det.lua")

local CLOCK = 0x84              -- Tennis's frame counter, $F170
local PORTS = {}
PORTS[0x0280] = "SWCHA"
PORTS[0x0282] = "SWCHB"
for _, base in ipairs({ 0x00, 0x10, 0x20, 0x30 }) do
    PORTS[base + 0x0C] = "INPT4"
    PORTS[base + 0x0D] = "INPT5"
end

local sp = manager.machine.devices[":maincpu"].spaces["program"]
local cpu = manager.machine.devices[":maincpu"]
local sites = {}

for addr, name in pairs(PORTS) do
    _G["_in_" .. name .. addr] = sp:install_read_tap(addr, addr, name,
        function(off, data, mask)
            -- THE PC READ INSIDE A TAP IS NOT THE INSTRUCTION'S: it has
            -- already moved on. Report the ADDRESS READ alongside it and let
            -- the operand size explain the offset, rather than chasing a
            -- number that belongs to an instruction two further along.
            local pc = cpu.state["PC"].value
            local k = string.format("%s @ pc~$%04X", name, pc)
            sites[k] = (sites[k] or 0) + 1
        end)
end

_G._in_end = emu.add_machine_stop_notifier(function()
    local ks = {}
    for k in pairs(sites) do ks[#ks + 1] = k end
    table.sort(ks)
    print("SITES")
    for _, k in ipairs(ks) do
        print(string.format("  %-28s x%d", k, sites[k]))
    end
    print(string.format("SITES TOTAL %d distinct", #ks))
end)
