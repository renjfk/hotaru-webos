-- rplab/scripts/pad_ps.lua - press PS on another controller index of the feedback history stream
-- (the low 2 bits of an event's first byte), optionally after announcing it with a ControllerConnection.
-- Usage: rp run pad_ps <index 1..3> [cc id|none] [type] [hold ms]
-- Shot: pad<index>-ps[-cc<id>].png 2.5 s after the press.
rp.show_log = false
local index = tonumber((select(1, ...))) or 1
local ccid = select(2, ...)
local ctype = select(3, ...) or "ds4"
local hold = tonumber((select(4, ...))) or 200
local b0 = 0x80 + index

rp.tap("circle", 2, 500)
rp.sleep(800)
local s = rp.seq()
if ccid and ccid ~= "none" then
    rp.cc{ id = tonumber(ccid), connected = 1, type = ctype }
    rp.sleep(1000)
end
rp.hist(("%02xae"):format(b0))
rp.sleep(hold)
rp.hist(("%02x8e"):format(b0))
rp.sleep(2500)
rp.shot(("pad%d-ps%s"):format(index, ccid and ccid ~= "none" and ("-cc" .. ccid) or ""))
for _, l in ipairs(rp.lines(s)) do
    if not l:find("Probe") and not l:find("%* >") then print("    " .. l) end
end
