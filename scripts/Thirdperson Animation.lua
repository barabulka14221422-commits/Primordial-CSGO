-- ============================================================================
-- PRIMORDIAL CS:GO THIRDPERSON ANIMATION
-- Fixed by @saved_messenges for @clanpidori
-- ============================================================================

local alive_thirdperson = menu.find("visuals","other","thirdperson","enable")
local cam_idealdist = menu.find("visuals","other","thirdperson","distance")

local thirdperson_animation = menu.add_checkbox("Effects", "Thirdperson animation")
local thirdperson_distance  = menu.add_slider("Effects", "Thirdperson distance", 30, 400)
local thirdperson_speed     = menu.add_slider("Effects", "Thirdperson speed", 1, 100)
thirdperson_distance:set(160)
thirdperson_speed:set(25)

local function animation(check, name, value, speed) 
    if check then 
        return name + (value - name) * global_vars.frame_time() * speed 
    else 
        return name - (value + name) * global_vars.frame_time() * speed
    end
end

local animation_amt = 0
local on_thirdperson_animation = function()
    if thirdperson_animation:get() then
        local is_enabled = false
        if alive_thirdperson then
            if type(alive_thirdperson) == "table" and alive_thirdperson[2] then
                is_enabled = alive_thirdperson[2]:get()
            elseif alive_thirdperson.get then
                is_enabled = alive_thirdperson:get()
            end
        end

        animation_amt = animation(is_enabled, animation_amt, thirdperson_distance:get(), thirdperson_speed:get())
        if animation_amt < 0 then animation_amt = 0 end
        if cam_idealdist then cam_idealdist:set(animation_amt) end

        pcall(function()
            if cvars.cam_idealdist then cvars.cam_idealdist:set_float(animation_amt) end
            if cvars.c_mindistance then cvars.c_mindistance:set_float(30) end
            if cvars.c_maxdistance then cvars.c_maxdistance:set_float(450) end
            if cvars.cam_collision then cvars.cam_collision:set_int(0) end
        end)
    end
end

callbacks.add(e_callbacks.PAINT, function()
    thirdperson_distance:set_visible(thirdperson_animation:get())
    thirdperson_speed:set_visible(thirdperson_animation:get())
    on_thirdperson_animation()
end)