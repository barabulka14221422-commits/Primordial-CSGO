-- ============================================================================
-- PRIMORDIAL CS:GO THIRDPERSON ZOOM OUT ANIMATION
-- Fixed by @saved_messenges for @clanpidori
-- ============================================================================

local third_p_dis = 0
local ref = {
    trd_p_key = menu.find("visuals", "other", "thirdperson", "enable"),
    trd_p = menu.find("visuals", "other", "thirdperson", "distance"),
}

local cus_trd_p = menu.add_slider("thirdperson", "thirdperson distance", 30, 400, 1.0, 0, "u")
local trd_p_out = menu.add_slider("thirdperson", "thirdperson fade time", 0, 100, 1.0, 0, "u")
cus_trd_p:set(160)
trd_p_out:set(10)

local function trd_person() 
    local fade_rate = trd_p_out:get() * 0.1
    if fade_rate <= 0.001 then fade_rate = 1.0 end
    local fade_time = (10 / fade_rate) * global_vars.frame_time()

    local is_enabled = false
    if ref.trd_p_key then
        if type(ref.trd_p_key) == "table" and ref.trd_p_key[2] then
            is_enabled = ref.trd_p_key[2]:get()
        elseif ref.trd_p_key.get then
            is_enabled = ref.trd_p_key:get()
        end
    end

    if is_enabled then
        if third_p_dis < 1.0 then
            third_p_dis = third_p_dis + (fade_time * (1 - third_p_dis))
        end
    else
        third_p_dis = 0
    end

    if third_p_dis > 1.0 then third_p_dis = 1.0 end

    local target = cus_trd_p:get() * third_p_dis
    if ref.trd_p then
        ref.trd_p:set(target)
    end

    pcall(function()
        if cvars.cam_idealdist then cvars.cam_idealdist:set_float(target) end
        if cvars.c_mindistance then cvars.c_mindistance:set_float(30) end
        if cvars.c_maxdistance then cvars.c_maxdistance:set_float(450) end
        if cvars.cam_collision then cvars.cam_collision:set_int(0) end
    end)
end

callbacks.add(e_callbacks.PAINT, trd_person)