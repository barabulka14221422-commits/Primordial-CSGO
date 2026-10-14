-- ============================================================================
-- PRIMORDIAL CS:GO THIRD PERSON DISTANCE FIX
-- coded by @saved_messenges for @clanpidori
-- Universal Support: CS:GO Legacy & CS:GO Beta
-- ============================================================================

local dist_slider = menu.add_slider("Thirdperson Fix", "Camera Distance", 30, 400)
local no_collision = menu.add_checkbox("Thirdperson Fix", "Disable Wall Collision", true)
dist_slider:set(160)

local og_distance = menu.find("visuals", "other", "thirdperson", "distance")
local og_enable   = menu.find("visuals", "other", "thirdperson", "enable")

local function update_tp()
    local target_dist = dist_slider:get()

    -- 1. Sync Primordial cheat distance directly without bad offsets
    if og_distance then
        pcall(function() og_distance:set(target_dist) end)
    end

    -- 2. Sync Engine camera convars (prevents clamping and collision sticking)
    pcall(function()
        if cvars.cam_idealdist then
            cvars.cam_idealdist:set_float(target_dist)
        end
        if cvars.c_mindistance then
            cvars.c_mindistance:set_float(30)
        end
        if cvars.c_maxdistance then
            cvars.c_maxdistance:set_float(math.max(400, target_dist + 150))
        end
        if cvars.cam_collision then
            cvars.cam_collision:set_int(no_collision:get() and 0 or 1)
        end
        if cvars.cam_command then
            cvars.cam_command:set_int(1)
        end
    end)
end

callbacks.add(e_callbacks.PAINT, function()
    pcall(update_tp)
end)

callbacks.add(e_callbacks.RUN_COMMAND, function()
    pcall(update_tp)
end)