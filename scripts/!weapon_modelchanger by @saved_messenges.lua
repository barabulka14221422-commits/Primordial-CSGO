-- ============================================================================
-- PRIMORDIAL CS:GO KNIFE MODEL CHANGER
-- !weapon_modelchanger coded by @saved_messenges for @clanpidori
-- Universal Support: CS:GO Legacy & CS:GO Beta
-- Auto-applies on inject. Octane Heirloom with Custom Sounds & Textures
-- Models:
--   Viewmodel:  models/weapons/k31/v_octaneheirloom.mdl
--   Worldmodel: models/weapons/k31/w_octaneheirloom.mdl
-- Materials:    materials/models/K31/ (octane_heirloom, octane_heirloom_glass)
-- Sounds:       sound/weapons/custom_weapon/k31/octane/ (8 custom wav sounds)
-- Crash-proof: Safe in main menu, no null pointer dereferences.
-- ============================================================================

local ffi = require("ffi")

-- Native Interfaces
local ent_list = memory.create_interface("client.dll", "VClientEntityList003")
local entity_list_raw = ent_list and ffi.cast("void***", ent_list) or nil

local model_info_interface = memory.create_interface("engine.dll", "VModelInfoClient004")
local raw_model_info = model_info_interface and ffi.cast("void***", model_info_interface) or nil

local string_table_container = memory.create_interface("engine.dll", "VEngineClientStringTable001")
local raw_string_tables = string_table_container and ffi.cast("void***", string_table_container) or nil

local get_client_entity = nil
if entity_list_raw then
    pcall(function()
        get_client_entity = ffi.cast("void*(__thiscall*)(void*,int)", memory.get_vfunc(ent_list, 3))
    end)
end

local get_model_index = nil
local find_or_load_model = nil
if raw_model_info then
    pcall(function()
        get_model_index = ffi.cast("int(__thiscall*)(void*, const char*)", memory.get_vfunc(tonumber(ffi.cast("unsigned int", raw_model_info)), 2))
        find_or_load_model = ffi.cast("void(__thiscall*)(void*, const char*)", memory.get_vfunc(tonumber(ffi.cast("unsigned int", raw_model_info)), 43))
    end)
end

local SetModelIndex = nil
pcall(function()
    SetModelIndex = function(instance, model_idx)
        if not instance or instance == ffi.NULL then return end
        local raw = ffi.cast("void***", instance)
        if raw == nil or raw == ffi.NULL or raw[0] == nil then return end
        local fn = ffi.cast("void(__thiscall*)(void*,int)", raw[0][75])
        if fn then fn(instance, model_idx) end
    end
end)

local function get_string_table(name)
    if not engine.is_connected() or not engine.is_in_game() then return nil end
    if not raw_string_tables or raw_string_tables == ffi.NULL then return nil end
    local ok, tbl = pcall(function()
        if raw_string_tables[0] == nil then return nil end
        local find_table_fn = ffi.cast("void*(__thiscall*)(void*, const char*)", raw_string_tables[0][3])
        if not find_table_fn then return nil end
        return find_table_fn(raw_string_tables, name)
    end)
    return (ok and tbl and tbl ~= ffi.NULL) and tbl or nil
end

local function add_to_string_table(tbl, str)
    if not engine.is_connected() or not engine.is_in_game() then return -1 end
    if not tbl or tbl == ffi.NULL or not str or str == "" then return -1 end
    local ok, res = pcall(function()
        local raw_tbl = ffi.cast("void***", tbl)
        if raw_tbl == nil or raw_tbl == ffi.NULL or raw_tbl[0] == nil then return -1 end
        local add_str_fn = ffi.cast("int(__thiscall*)(void*, bool, const char*, int, const void*)", raw_tbl[0][8])
        if not add_str_fn then return -1 end
        return add_str_fn(tbl, false, str, -1, nil)
    end)
    return ok and res or -1
end

local KNIFE_MODEL = "models/weapons/k31/v_octaneheirloom.mdl"
local KNIFE_WORLD_MODEL = "models/weapons/k31/w_octaneheirloom.mdl"

local knife_idx = nil
local knife_world_idx = nil

local OCTANE_SOUNDS = {
    "weapons/custom_weapon/k31/octane/drawbut.wav",
    "weapons/custom_weapon/k31/octane/inspect.wav",
    "weapons/custom_weapon/k31/octane/Inspect_V1_2ch_p5.wav",
    "weapons/custom_weapon/k31/octane/Melee_Jump_V2_2ch_01.wav",
    "weapons/custom_weapon/k31/octane/Melee_Jump_V2_2ch_02.wav",
    "weapons/custom_weapon/k31/octane/Melee_Slide_01_V1_2ch_01.wav",
    "weapons/custom_weapon/k31/octane/Melee_Sprinting_V1_2ch_01.wav",
    "weapons/custom_weapon/k31/octane/Melee_Stand_01_V1_2ch_02.wav"
}

local function precache_model_universal(path)
    if not engine.is_connected() or not engine.is_in_game() then return nil end
    if not path or #path <= 3 then return nil end
    local norm = string.gsub(path, "\\", "/")
    local idx = nil

    pcall(function()
        if raw_model_info and get_model_index then
            idx = get_model_index(raw_model_info, norm)
        end
    end)

    if not idx or idx <= 0 then
        pcall(function() client.precache_model(norm) end)

        pcall(function()
            if raw_model_info and find_or_load_model then
                find_or_load_model(raw_model_info, norm)
            end
        end)

        pcall(function()
            local precache_tbl = get_string_table("modelprecache")
            if precache_tbl then
                add_to_string_table(precache_tbl, norm)
            end
        end)

        pcall(function()
            if raw_model_info and get_model_index then
                idx = get_model_index(raw_model_info, norm)
            end
        end)
    end

    return (idx and idx > 0) and idx or nil
end

local sounds_precached = false
local function precache_all_sounds()
    if not engine.is_connected() or not engine.is_in_game() then return end
    if sounds_precached then return end
    pcall(function()
        local snd_tbl = get_string_table("soundprecache")
        for _, snd in ipairs(OCTANE_SOUNDS) do
            if snd_tbl then add_to_string_table(snd_tbl, snd) end
            pcall(function() client.precache_sound(snd) end)
        end
        engine.execute_cmd("snd_updateaudiocache")
        sounds_precached = true
    end)
end

local function get_knife_indices()
    if not engine.is_connected() or not engine.is_in_game() then return nil, nil end
    if not knife_idx or knife_idx <= 0 then
        knife_idx = precache_model_universal(KNIFE_MODEL)
    end
    if not knife_world_idx or knife_world_idx <= 0 then
        knife_world_idx = precache_model_universal(KNIFE_WORLD_MODEL)
    end
    precache_all_sounds()
    return knife_idx, knife_world_idx
end

local function reset_cache()
    knife_idx = nil
    knife_world_idx = nil
    sounds_precached = false
end

local function is_knife_def(d)
    return d == 42 or d == 59 or (d >= 500 and d <= 525)
end

local function get_active_weapon(lp)
    if not lp then return nil end
    local w = nil
    pcall(function()
        if lp.get_active_weapon then w = lp:get_active_weapon() end
        if not w then
            local h = lp:get_prop("m_hActiveWeapon")
            if h then w = entity_list.get_entity(h) end
        end
    end)
    return w
end

local function active_weapon_is_knife(lp)
    if not lp then return false, nil end
    local w = get_active_weapon(lp)
    if not w then return false, nil end
    local is_k = false
    pcall(function()
        local raw = w:get_prop("m_iItemDefinitionIndex")
        if raw and is_knife_def(bit.band(raw, 0xFFFF)) then
            is_k = true
            return
        end
        if w.get_classname then
            local cn = (w:get_classname() or ""):lower()
            if cn:find("knife") or cn:find("bayonet") then
                is_k = true
            end
        end
    end)
    return is_k, w
end

local last_weapon_handle = nil
local last_is_knife = false
local last_attack_time = 0

local function play_custom_knife_sound(sound_name)
    if not engine.is_connected() or not engine.is_in_game() then return end
    pcall(function()
        if engine.play_sound then
            engine.play_sound(sound_name)
        else
            engine.execute_cmd("play " .. sound_name)
        end
    end)
end

local function apply_knife_model(lp, w, model_idx, world_idx)
    if not lp or not w or not model_idx or model_idx <= 0 then return end

    -- 1. Apply to Viewmodel
    local vm_ent = nil
    pcall(function()
        local h = lp:get_prop("m_hViewModel[0]")
        if h then vm_ent = entity_list.get_entity(h) end
    end)

    if vm_ent then
        pcall(function() vm_ent:set_prop("m_nModelIndex", model_idx) end)
        pcall(function()
            if entity_list_raw and get_client_entity and SetModelIndex then
                local idx = vm_ent:get_index()
                if idx then
                    local ptr = ffi.cast("void***", get_client_entity(entity_list_raw, idx))
                    if ptr and ptr ~= ffi.NULL and ptr[0] ~= nil then SetModelIndex(ptr, model_idx) end
                end
            end
        end)
    end

    -- 2. Apply to Weapon entity
    pcall(function() w:set_prop("m_nModelIndex", model_idx) end)
    pcall(function() w:set_prop("m_iViewModelIndex", model_idx) end)
    pcall(function()
        if entity_list_raw and get_client_entity and SetModelIndex then
            local idx = w:get_index()
            if idx then
                local ptr = ffi.cast("void***", get_client_entity(entity_list_raw, idx))
                if ptr and ptr ~= ffi.NULL and ptr[0] ~= nil then SetModelIndex(ptr, model_idx) end
            end
        end
    end)

    -- 3. Apply World Model for Thirdperson
    if world_idx and world_idx > 0 then
        pcall(function() w:set_prop("m_iWorldModelIndex", world_idx) end)
        pcall(function()
            local h_wm = w:get_prop("m_hWeaponWorldModel")
            if h_wm then
                local wm_ent = entity_list.get_entity(h_wm)
                if wm_ent then
                    wm_ent:set_prop("m_nModelIndex", world_idx)
                    if entity_list_raw and get_client_entity and SetModelIndex then
                        local wmidx = wm_ent:get_index()
                        if wmidx then
                            local wmptr = ffi.cast("void***", get_client_entity(entity_list_raw, wmidx))
                            if wmptr and wmptr ~= ffi.NULL and wmptr[0] ~= nil then SetModelIndex(wmptr, world_idx) end
                        end
                    end
                end
            end
        end)
    end
end

local function run()
    if not engine.is_connected() or not engine.is_in_game() then
        last_is_knife = false
        last_weapon_handle = nil
        return
    end

    local lp = entity_list.get_local_player()
    if not lp or not lp:is_alive() then
        last_is_knife = false
        last_weapon_handle = nil
        return
    end

    local is_k, w = active_weapon_is_knife(lp)
    local cur_w_handle = lp:get_prop("m_hActiveWeapon")

    -- Detect knife equip / switch to play draw sound
    if is_k and (not last_is_knife or cur_w_handle ~= last_weapon_handle) then
        play_custom_knife_sound("weapons/custom_weapon/k31/octane/drawbut.wav")
    end

    last_is_knife = is_k
    last_weapon_handle = cur_w_handle

    if not is_k then return end

    local v_idx, w_idx = get_knife_indices()
    if not v_idx or v_idx <= 0 then return end

    apply_knife_model(lp, w, v_idx, w_idx)
end

-- Sound triggers for user actions (attack / inspect)
local function on_setup_command(cmd)
    if not engine.is_connected() or not engine.is_in_game() then return end
    if not last_is_knife then return end

    local lp = entity_list.get_local_player()
    if not lp or not lp:is_alive() then return end

    local now = global_vars.real_time()

    local is_attacking = false
    pcall(function()
        if cmd.has_button then
            is_attacking = cmd:has_button(e_buttons.ATTACK) or cmd:has_button(e_buttons.ATTACK2)
        elseif cmd.buttons then
            is_attacking = bit.band(cmd.buttons, 1) ~= 0 or bit.band(cmd.buttons, 2048) ~= 0
        end
    end)

    if is_attacking and (now - last_attack_time > 0.45) then
        last_attack_time = now
        local sounds = {
            "weapons/custom_weapon/k31/octane/Melee_Jump_V2_2ch_01.wav",
            "weapons/custom_weapon/k31/octane/Melee_Jump_V2_2ch_02.wav",
            "weapons/custom_weapon/k31/octane/Melee_Stand_01_V1_2ch_02.wav"
        }
        local pick = sounds[math.random(1, #sounds)]
        play_custom_knife_sound(pick)
    end
end

callbacks.add(e_callbacks.PAINT,         function() pcall(run) end)
callbacks.add(e_callbacks.NET_UPDATE,    function() pcall(run) end)
callbacks.add(e_callbacks.SETUP_COMMAND, function(cmd) pcall(on_setup_command, cmd) end)

callbacks.add(e_callbacks.EVENT, function() pcall(reset_cache) end, "player_team")
callbacks.add(e_callbacks.EVENT, function() pcall(reset_cache) end, "round_start")
callbacks.add(e_callbacks.EVENT, function() pcall(reset_cache) end, "player_spawn")
