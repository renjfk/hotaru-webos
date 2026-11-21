-- rplab/scripts/accessories.lua - open the PS5 Control Center's Accessories card and screenshot it.
-- Usage: rp run accessories [name]
local name = ... or "accessories"
rp.cc_open()
if not rp.cc_goto("accessories") then print("could not focus Accessories") end
rp.press("cross")
rp.sleep(1200)
rp.shot(name)
rp.press("circle")
rp.sleep(400)
rp.press("circle")
