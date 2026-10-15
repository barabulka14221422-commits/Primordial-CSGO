-- ============================================================================
-- PRIMORDIAL CS:GO THIRDPERSON CRASH FIX
-- Fixed by @saved_messenges for @clanpidori
-- Crash-proof: Zero execution in main menu!
-- ============================================================================

local menugroup = menu.set_group_column("Menu", 1) 
local tptoggle = menu.add_checkbox("Menu", "Thirdperson Fix", false)
local tpslider = menu.add_slider("Menu", "Thirdperson Distance", 30, 400, 1, 0)
local coltoggle = menu.add_checkbox("Menu", "Thirdperson Collision", false)
tpslider:set(160)

local keybind = tptoggle:add_keybind("Thirdperson Fix")

function tphandle()
    if not engine.is_connected() or not engine.is_in_game() then return end
    local lp = entity_list.get_local_player()
    if not lp or not lp:is_alive() then return end

    if cvars.sv_cheats then pcall(function() cvars.sv_cheats:set_int(1) end) end

    if keybind:get() then
        pcall(function() cvars.cam_command:set_int(1) end)
    else 
        pcall(function() cvars.cam_command:set_int(0) end)
    end

    if coltoggle:get() then
        pcall(function() cvars.cam_collision:set_int(1) end)
    else 
        pcall(function() cvars.cam_collision:set_int(0) end)
    end

    local distance = tpslider:get()
    pcall(function()
        cvars.c_mindistance:set_int(30)
        cvars.c_maxdistance:set_int(distance + 150)
        cvars.cam_idealdist:set_float(distance)
    end)
end

callbacks.add(e_callbacks.PAINT, function()
    pcall(tphandle)
end)