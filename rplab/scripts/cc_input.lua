-- rplab/scripts/cc_input.lua - does the console act on a ControllerConnection sent mid-session?
-- With the Control Center's icon row focused, applies each variant, taps right and reads whether the
-- focus moved (the session's pad still drives the console), then restores the pad.
rp.show_log = false
rp.cc_open()
rp.cc_goto("gamebase")

local function moves()
    local a = rp.cc_focus()
    rp.tap("right", 1)
    rp.sleep(300)
    local b = rp.cc_focus()
    rp.cc_goto("gamebase")
    return a ~= b, a, b
end

local function try(tag, msg)
    local s = rp.seq()
    if msg then rp.cc(msg) end
    rp.sleep(1000)
    local moved, a, b = moves()
    print(("%-28s input %s (focus %s -> %s)"):format(tag, moved and "WORKS  " or "BLOCKED", a, b))
    for _, l in ipairs(rp.lines(s)) do
        if not l:find("Probe controller connection") and not l:find("%* >") then print("    " .. l) end
    end
end

try("baseline")
try("no id, connected=0", { connected = 0 })
try("no id, connected=1", { connected = 1 })
try("id=0, connected=0", { id = 0, connected = 0 })
try("id=0, connected=1", { id = 0, connected = 1 })
try("id=1, connected=0", { id = 1, connected = 0 })
try("id=1, connected=1", { id = 1, connected = 1 })
try("after all")
