-- slack.lua -- how many cycles the one timed band actually has left.
--
-- Tennis arms TIM64T at $F1A2 with $2D -- 2880 cycles -- and spins it out at
-- $F051, with the whole game logic and the per-frame display setup in between.
-- The netcode lives in what is left. This measures it, on stock and on the
-- build, because PORTING.md's rule is that both numbers are measured and
-- neither is assumed.
--
-- INTIM is read ONLY by that spin, so the first read of each frame is the
-- answer: multiply by 64 and that is the slack in cycles. (Below 1 the timer
-- has already underflowed and the answer is zero or worse -- which is the
-- case the gate is really for.)
--
--   SLOT=a26_2k_4k ./run.sh stock slack
--   ./run.sh tennis slack
local FLOOR = tonumber(os.getenv("SLACK_FLOOR") or "") or 8

local sp = manager.machine.devices[":maincpu"].spaces["program"]
local frames, worst, total, zero = 0, 255, 0, 0
local pending = true

_G._sl = sp:install_read_tap(0x0284, 0x0284, "intim", function(off, data, mask)
    if not pending then return end      -- only the FIRST read of the frame
    pending = false
    frames = frames + 1
    if data < worst then worst = data end
    total = total + data
    if data == 0 then zero = zero + 1 end
end)

-- The frame boundary that re-arms the sampler is the timer write, which
-- happens exactly once per frame at $F1A2 and cannot be confused with
-- anything else -- the RAM clear sweeps the TIA but never the RIOT's $0296.
_G._sl2 = sp:install_write_tap(0x0296, 0x0296, "tim64t", function()
    pending = true
end)

_G._sl_end = emu.add_machine_stop_notifier(function()
    if frames == 0 then print("SLACK FAIL: no frames sampled"); return end
    print(string.format(
        "SLACK %d frames: worst %d ticks = %d cycles, mean %.1f ticks = %d cycles, %d exhausted",
        frames, worst, worst * 64, total / frames,
        math.floor(total / frames) * 64, zero))
    if worst >= FLOOR then
        print(string.format("SLACK PASS -- the floor is %d ticks (%d cycles) "
                            .. "and the worst frame left %d", FLOOR, FLOOR * 64, worst))
    else
        print(string.format("SLACK FAIL -- the worst frame left %d ticks, "
                            .. "under the floor of %d", worst, FLOOR))
    end
end)
