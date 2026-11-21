-- rplab/scripts/setup_matrix.lua - vary the session setup, then look for a second controller.
-- Usage: rp run setup_matrix <batch> (see BATCHES); one reconnect per variant (~25 s each).
-- Per variant: reconnects with rp.setup(variant), prints the console's session and ctrl response headers
-- that differ from the defaults' and whether the stream came up, then presses PS on controller index 1
-- (feedback history 0x81) and on pad 0 as the control, flagging any change on screen (shots on change).
rp.show_log = false
local batch = (...) or "headers"

local BATCHES = {
    headers = {
        { name = "default" },
        { name = "SupportCmd 142300", ctrl = { "RP-SupportCmd: 142300" } },
        { name = "ReqMoreInfo 1", ctrl = { "RP-ReqMoreInfo: 1" } },
        { name = "Option 1", ctrl = { "RP-Option: 1" } },
        { name = "SupportCmd+ReqMoreInfo+Option", ctrl = { "RP-SupportCmd: 142300", "RP-ReqMoreInfo: 1", "RP-Option: 1" },
          init = { "RP-SupportCmd: 142300" } },
    },
    supportcmd = {
        { name = "SupportCmd 142300 (again)", ctrl = { "RP-SupportCmd: 142300" } },
        { name = "SupportCmd ffffff", ctrl = { "RP-SupportCmd: ffffff" } },
        { name = "SupportCmd ffffffff", ctrl = { "RP-SupportCmd: ffffffff" } },
        { name = "SupportCmd 1", ctrl = { "RP-SupportCmd: 1" } },
        { name = "init SupportCmd 142300 only", init = { "RP-SupportCmd: 142300" } },
        { name = "SupportCmd 142300 + ReqMoreInfo 1", ctrl = { "RP-SupportCmd: 142300", "RP-ReqMoreInfo: 1" } },
    },
    supportver = {
        { name = "SupportCmd 142299", ctrl = { "RP-SupportCmd: 142299" } },
        { name = "SupportCmd 142301", ctrl = { "RP-SupportCmd: 142301" } },
        { name = "SupportCmd 200000", ctrl = { "RP-SupportCmd: 200000" } },
        { name = "SupportCmd 999999", ctrl = { "RP-SupportCmd: 999999" } },
        { name = "SupportCmd 100000", ctrl = { "RP-SupportCmd: 100000" } },
    },
    clienttype = (function()
        local t = {}
        for _, ct in ipairs({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16 }) do
            t[#t + 1] = { name = "ClientType " .. ct .. " + all", client_type = ct, ctrl = { "RP-SupportCmd: 142300" },
                          controllers = { "ds4", "ds4", "ds4", "ds4" }, start_cc = { 1, 2, 3 }, start_cc_type = "ds4" }
        end
        return t
    end)(),
    padproc = {
        { name = "PadProcNo none", pad_proc_no = "none" },
        { name = "PadProcNo 1", pad_proc_no = 1 },
        { name = "PadProcNo 2", pad_proc_no = 2 },
        { name = "PadProcNo 4", pad_proc_no = 4 },
    },
    ctype = {
        { name = "ControllerType 0", controller_type = 0 },
        { name = "ControllerType 1", controller_type = 1 },
        { name = "ControllerType 2", controller_type = 2 },
        { name = "ControllerType 4", controller_type = 4 },
        { name = "ControllerType 5", controller_type = 5 },
        { name = "ControllerType 6", controller_type = 6 },
    },
    launch = {
        { name = "controllers ds4 x2", controllers = { "ds4", "ds4" } },
        { name = "controllers ds4 x4", controllers = { "ds4", "ds4", "ds4", "ds4" } },
        { name = "start_cc 1", start_cc = { 1 }, start_cc_type = "ds4" },
        { name = "start_cc 1-3, id0", start_cc = { 1, 2, 3 }, start_cc_type = "ds4", id0 = true },
        { name = "ds4 x2 + start_cc 1", controllers = { "ds4", "ds4" }, start_cc = { 1 }, start_cc_type = "ds4" },
    },
}



local function response_headers(since)
    local h = {}
    for _, l in ipairs(rp.lines(since)) do
        local kind, k, v = l:match("Probe (%a+) response header ([%w%-]+): (.*)$")
        if kind then h[kind .. " " .. k] = v end
        local code = l:match("Probe %a+ response: HTTP (%d+)")
        if code and code ~= "200" then h[l:match("Probe (%a+) response") .. " HTTP"] = code end
    end
    return h
end

local function press_ps(index)
    rp.hist(("%02xae"):format(0x80 + index))
    rp.sleep(200)
    rp.hist(("%02x8e"):format(0x80 + index))
end

local function reaction(tag, index)
    rp.tap("circle", 2, 400)
    rp.sleep(1500)
    local g0 = rp.grid()
    rp.sleep(1000)
    local g1 = rp.grid()
    local noise = rp.diff(g0, g1)
    press_ps(index)
    rp.sleep(2500)
    local g2 = rp.grid()
    local max, n = rp.diff(g1, g2)
    local changed = max > noise + 20 and n >= 3
    if changed then rp.shot(tag:gsub("[^%w]+", "_") .. "-pad" .. index) end
    return changed, max, n, noise
end

local function run(v)
    rp.setup(v)
    local s = rp.seq()
    local ok, err = pcall(rp.reconnect)
    local info = rp.info()
    local h = response_headers(s)
    if v.name == "default" then rp.setup_baseline = h end
    local hdiff = {}
    local random = { ["ctrl RP-Server-Type"] = true, ["session RP-Nonce"] = true }
    for k, val in pairs(h) do
        if not random[k] and (not rp.setup_baseline or rp.setup_baseline[k] ~= val) then hdiff[#hdiff + 1] = k .. "=" .. val end
    end
    table.sort(hdiff)
    print(("== %s: %s%s"):format(v.name, info.state, info.state ~= "connected" and (" (" .. info.quit_reason .. ")") or ""))
    if #hdiff > 0 then print("   headers: " .. table.concat(hdiff, ", ")) end
    local usual = { ["0x5"] = true, ["0x16"] = true, ["0x33"] = true, ["0x41"] = true, ["0xfe"] = true }
    local lines = rp.lines(s)
    for i, l in ipairs(lines) do
        local t = l:match("Ctrl received message of type (0x%x+)")
        if t and not usual[t] then print("   ctrl message " .. t .. ": " .. ((lines[i + 2] or ""):match("     0 (.-)%s%s") or "?")) end
    end
    if not ok or info.state ~= "connected" then return end
    rp.sleep(1500)
    local c1, m1, n1, z1 = reaction(v.name, 1)
    local c0, m0, n0 = reaction(v.name, 0)
    print(("   pad1 PS: %s (max %.0f, %d cells, noise %.0f)   pad0 PS control: %s (max %.0f, %d cells)"):format(
        c1 and "SCREEN CHANGED" or "nothing", m1, n1, z1, c0 and "changed" or "NO CHANGE", m0, n0))
    rp.tap("circle", 2, 400)
    for _, l in ipairs(rp.lines(s)) do
        if (l:find("EVENT") or l:find(" [WE] ")) and not l:find("Probe") and not l:find("motion_reset")
            and not l:find("EVENT connected") and not l:find("already in use") and not l:find("quit %(Stopped%)")
            and not l:find("offset 0  1") and not l:find("     0 ") then
            print("   " .. l)
        end
    end
end


for _, v in ipairs(BATCHES[batch] or error("no batch " .. batch)) do run(v) end
rp.setup()
