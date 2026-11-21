-- rplab/prelude.lua - Lua helpers on rpd's native core; read again on `reload` (see rpd.c).
--
-- Native core:
--   rp.cmd(line) -> reply    any daemon command (status, press, pad0, states, slot, cc, hist, shot, wait, ...)
--   rp.sleep(ms)             wait while the session runs on
--   rp.now() -> ms           since the daemon started
--   rp.seq() -> n            the notable log's position, for rp.lines(n)
--   rp.lines([n]) -> {...}   notable log lines since position n
--   rp.info() -> {...}       state, player_index, led, frames_in, frames_out, quit_reason, host
--   rp.fresh([ms]) -> bool   wait for a frame decoded after the call
--   rp.luma(x, y, w, h)      mean luma of a region of the latest frame (screenshot coordinates, 1280x720)
--   rp.setup{...}            session setup knobs for the next connect (see rpd.c l_setup); rp.setup() resets
--   rp.dir, rp.lua_dir       the state dir (shots/, logs) and this file's dir (scripts/ is on package.path)
--   rp.show_log              true: a script's reply ends with the log lines it caused
--   print(...)               into the reply

local function check(reply)
    if reply:sub(1, 3) == "err" then error(reply, 3) end
    return reply
end

local function kvs(t, keys)
    local parts = {}
    for _, k in ipairs(keys) do
        if t[k] ~= nil then parts[#parts + 1] = k .. "=" .. tostring(t[k]) end
    end
    return table.concat(parts, " ")
end

function rp.status() return rp.cmd("status") end

-- wait until the session is up (the daemon connects on start and retries while the console is busy)
function rp.wait_connected(ms)
    local t = rp.now()
    while rp.info().state ~= "connected" and rp.now() - t < (ms or 60000) do rp.sleep(250) end
    return rp.info().state == "connected"
end

-- put the console into rest mode; the session ends (wake, then connect, to come back)
function rp.rest() return check(rp.cmd("rest")) end

-- end the session and connect again (rp.setup applies here); waits out the console's ~15 s refusal
function rp.reconnect() return check(rp.cmd("reconnect")) end

-- a coarse picture of the screen: mean luma of a 16x9 grid of 80x80 cells
function rp.grid()
    rp.fresh()
    local g = {}
    for y = 0, 8 do
        for x = 0, 15 do
            g[#g + 1] = rp.luma(x * 80, y * 80, 80, 80) or 0
        end
    end
    return g
end

-- the largest cell change between two grids, and how many cells changed by more than 8
function rp.diff(a, b)
    local max, n = 0, 0
    for i = 1, math.min(#a, #b) do
        local d = math.abs(a[i] - b[i])
        if d > max then max = d end
        if d > 8 then n = n + 1 end
    end
    return max, n
end

-- rp.press("cross"), rp.press("l1,r1", 300)
function rp.press(buttons, ms) return check(rp.cmd(("press %s %d"):format(buttons, ms or 150))) end

-- rp.tap("right", 8): press one button n times (the PS5 UI drops presses closer than ~250 ms apart)
function rp.tap(button, n, gap)
    for _ = 1, n or 1 do
        rp.press(button, 100)
        rp.sleep(gap or 300)
    end
end

-- rp.nav("left*12 right*8 cross"): a sequence of taps
function rp.nav(spec)
    for tok in spec:gmatch("%S+") do
        local b, n = tok:match("^([%w,]+)%*(%d+)$")
        rp.tap(b or tok, tonumber(n) or 1)
    end
end

-- rp.pad0{buttons = "cross", ly = 32767}, rp.pad0("idle")
function rp.pad0(t)
    if t == "idle" then return check(rp.cmd("pad0 idle")) end
    return check(rp.cmd("pad0 " .. kvs(t, { "buttons", "lx", "ly", "rx", "ry", "l2", "r2" })))
end

-- rp.states(2), rp.states(2, 0) (header byte 3 forced), rp.states(1, "auto")
function rp.states(n, byte3)
    return check(rp.cmd(("states %d%s"):format(n, byte3 ~= nil and (" byte3=" .. tostring(byte3)) or "")))
end

-- rp.slot(1, {ly = 32767}), rp.slot(1, {raw = "a0ff7f..."}), rp.slot(1, "clear")
function rp.slot(i, t)
    if t == "clear" then return check(rp.cmd(("slot %d clear"):format(i))) end
    return check(rp.cmd(("slot %d %s"):format(i, kvs(t, { "raw", "lx", "ly", "rx", "ry" }))))
end

-- ControllerConnection: rp.cc{id = 1, connected = true, type = "ds4"}; id = "none" sends no id
function rp.cc(t)
    t = t or {}
    local parts = { "cc" }
    if t.id ~= nil then parts[#parts + 1] = "id=" .. tostring(t.id) end
    if t.connected ~= nil then parts[#parts + 1] = "connected=" .. ((t.connected == true or t.connected == 1) and "1" or "0") end
    if t.type ~= nil then parts[#parts + 1] = "type=" .. tostring(t.type) end
    return check(rp.cmd(table.concat(parts, " ")))
end

-- one raw feedback history event: rp.hist("80 88 ff")
function rp.hist(hex) return check(rp.cmd("hist " .. hex:gsub("%s", ""))) end

-- screenshot of the console's screen -> path (also printed)
function rp.shot(name)
    local path = check(rp.cmd("shot " .. (name or ""))):match("ok shot (%S+)")
    print("shot " .. path)
    return path
end

-- the notable lines that arrive in the next ms
function rp.wait(ms)
    local s = rp.seq()
    rp.sleep(ms)
    return rp.lines(s)
end

-- run fn and return the notable lines it caused within ms
function rp.watch(ms, fn)
    local s = rp.seq()
    if fn then fn() end
    rp.sleep(ms or 500)
    return rp.lines(s)
end

-- the first line since position since matching a Lua pattern
function rp.find(pattern, since)
    for _, l in ipairs(rp.lines(since)) do
        if l:find(pattern) then return l end
    end
end

function rp.dump(lines)
    for _, l in ipairs(lines) do print(l) end
end

-- PS5 Control Center: a row of 11 icons (720p screenshot coordinates) that wraps around; the console
-- remembers the last focused icon, so navigation reads the focus off the screen instead of counting
rp.CC_ICONS = { "home", "game", "notifications", "gamebase", "music", "downloads", "sound", "mic", "accessories",
                "user", "power" }
local CC_X0, CC_DX, CC_Y = 267, 74.6, 675

-- the focused icon's number, or nil when the icon row is not on screen with focus: the console labels
-- only the focused icon, and the strip under the labels is flat elsewhere (other screens are not)
function rp.cc_focus()
    rp.fresh()
    local label, lat, lmax = {}, nil, -1
    for i = 1, #rp.CC_ICONS do
        local x = math.floor(CC_X0 + (i - 1) * CC_DX + 0.5)
        label[i] = rp.luma(x - 30, CC_Y - 49, 60, 14) or 0
        if label[i] > lmax then lmax, lat = label[i], i end
    end
    local others = {} -- a long label ("Downloads/Uploads") spills over the neighbours' strips
    for i, l in ipairs(label) do
        if math.abs(i - lat) > 2 then others[#others + 1] = l end
    end
    table.sort(others)
    local lo, hi = others[1], others[#others]
    if hi - lo <= 4 and lmax - hi >= 6 then return lat, label end
    return nil, label
end

-- open the Control Center with the icon row focused, from wherever the console is: back out of
-- cards and the Control Center first (PS would close an open one), then PS; true once it is verified
function rp.cc_open()
    for _ = 1, 2 do
        if rp.cc_focus() then return true end
        rp.tap("circle", 2, 500)
        rp.sleep(500)
        rp.press("ps")
        rp.sleep(1500)
        rp.nav("down*2")
        rp.sleep(300)
    end
    return rp.cc_focus() ~= nil
end

-- move the focus to icon n (a number or a name in rp.CC_ICONS), the short way round
function rp.cc_goto(n)
    if type(n) == "string" then
        for i, name in ipairs(rp.CC_ICONS) do
            if name == n then n = i end
        end
    end
    for _ = 1, 3 do
        local at = rp.cc_focus()
        if not at then
            if not rp.cc_open() then return false end
            at = rp.cc_focus()
        end
        if at == n then return true end
        local d = (n - at) % #rp.CC_ICONS
        if d <= #rp.CC_ICONS / 2 then rp.tap("right", d) else rp.tap("left", #rp.CC_ICONS - d) end
        rp.sleep(300)
    end
    return rp.cc_focus() == n
end
