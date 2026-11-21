-- rplab/scripts/stick_states.lua - does the console read the extra states of a feedback state packet?
-- With the Control Center's icon row focused, holds the left stick right in one place (pad 0 itself as
-- the control, then an extra slot) and reads whether the focus moved.
rp.show_log = false
local HOLD_MS = 350 -- one or two steps; a long hold can wrap all 11 icons round to the start

local function reset()
    rp.pad0("idle")
    rp.states(1, "auto")
    for i = 1, 3 do rp.slot(i, "clear") end
end

local function try(tag, apply, clear)
    reset()
    if not rp.cc_goto("gamebase") then
        print(("%-34s INVALID (no Control Center)"):format(tag))
        return
    end
    rp.sleep(400)
    local s = rp.seq()
    local a = rp.cc_focus()
    apply()
    rp.sleep(HOLD_MS)
    clear()
    rp.sleep(700)
    local b = rp.cc_focus()
    local verdict = not b and "INVALID" or (a ~= b and "MOVED " or "stayed")
    print(("%-34s %s (focus %s -> %s)"):format(tag, verdict, a, b))
    for _, l in ipairs(rp.lines(s)) do
        if l:find("EVENT") or l:find("Pad info") or l:find(" [WE] ") then print("    " .. l) end
    end
end

rp.cc_open()
try("pad0 lx (control)", function() rp.pad0{ lx = 32767 } end, function() rp.pad0("idle") end)
try("2 states, slot 1 lx", function() rp.states(2); rp.slot(1, { lx = 32767 }) end, function() rp.slot(1, { lx = 0 }) end)
try("2 states (byte3=0), slot 1 lx", function() rp.states(2, 0); rp.slot(1, { lx = 32767 }) end,
    function() rp.slot(1, { lx = 0 }) end)
try("4 states, slot 3 lx", function() rp.states(4); rp.slot(3, { lx = 32767 }) end, function() rp.slot(3, { lx = 0 }) end)
try("4 states, slots 1-3 lx", function()
    rp.states(4)
    for i = 1, 3 do rp.slot(i, { lx = 32767 }) end
end, function() for i = 1, 3 do rp.slot(i, { lx = 0 }) end end)
try("pad0 lx (control again)", function() rp.pad0{ lx = 32767 } end, function() rp.pad0("idle") end)
reset()
