-- ============================================================================
-- Model Changer by @saved_messenges (tg)
-- Universal Support: CS:GO Legacy & CS:GO Beta
-- Top Verified Custom Player Models + Network String Table Precache
-- Supports Fox Miku, Haku, Yoshino, Hu Tao / Z3, Felix, Bismarck, Astolfo, Kirito, Neptune, Hibiki, Prinz Eugen
-- Includes Dynamic Folder Explorer Opener
-- Crash-proof: Zero precaching in main menu! Only runs in-game when connected.
-- ============================================================================

local ffi = require("ffi")

-- Verified Working Custom Player Models Database
local custom_models_db = {
    { 
        name = "Fox Miku", 
        path = "models/player/custom_player/monsterko/fox_miku/fox_miku.mdl",
        arms_path = "models/player/custom_player/monsterko/fox_miku/fox_miku_arms.mdl"
    },
    { 
        name = "Haku Wedding Dress", 
        path = "models/player/custom_player/monsterko/haku_wedding_dress/haku_v3.mdl",
        arms_path = "models/player/custom_player/monsterko/haku_wedding_dress/haku_arms.mdl"
    },
    { 
        name = "Yoshino", 
        path = "models/player/custom_player/monsterko/yoshino/yoshino.mdl",
        arms_path = "models/player/custom_player/monsterko/yoshino/yoshino_arms.mdl"
    },
    { 
        name = "Hu Tao (Genshin) / Z3", 
        path = "models/player/custom_player/toppiofficial/genshin/rework/hutao.mdl",
        alt_path = "models/player/custom_player/toppiofficial/abyss_front/z3.mdl",
        arms_path = "models/player/custom_player/toppiofficial/genshin/rework/hutao_arms.mdl",
        alt_arms = "models/player/custom_player/toppiofficial/abyss_front/z3_arms.mdl"
    },
    { 
        name = "Felix (Re:Zero)", 
        path = "models/player/custom_player/owston/rezero/felixtrap3.mdl",
        arms_path = "models/player/custom_player/owston/rezero/arms_felix3.mdl"
    },
    { 
        name = "Bismarck (Azur Lane)", 
        path = "models/player/custom_player/arcaea/azurelane/bismarck.mdl",
        arms_path = "models/player/custom_player/arcaea/azurelane/bismarck_arms.mdl"
    },
    { 
        name = "Astolfo (Fate/Apocrypha)", 
        path = "models/player/custom_player/legacy/gxp/anime/astolfo/astolfo_v1.mdl",
        arms_path = "models/player/custom_player/legacy/gxp/anime/astolfo/astolfo_arm_v1.mdl"
    },
    { 
        name = "Kirito (SAO)", 
        path = "models/player/custom_player/cg/kirito/kirito.mdl",
        arms_path = "models/player/custom_player/cg/kirito/kirito_arms.mdl"
    },
    { 
        name = "Adult Neptune", 
        path = "models/player/custom_player/kuristina/adult_neptune/adult_neptune.mdl",
        arms_path = "models/player/custom_player/kuristina/adult_neptune/adult_neptune_arms.mdl"
    },
    { 
        name = "Hibiki Maid Outfit", 
        path = "models/player/custom_player/kuristina/hibiki_maid/hibiki_maid.mdl",
        arms_path = "models/player/custom_player/kuristina/hibiki_maid/hibiki_maid_arms.mdl"
    },
    { 
        name = "Prinz Eugen (Azur Lane)", 
        path = "models/player/custom_player/kuristina/prinz_eugen/prinz_eugen.mdl",
        arms_path = "models/player/custom_player/kuristina/prinz_eugen/prinz_eugen_arms.mdl"
    }
}

-- Native FFI Memory Handles
local ent_list = memory.create_interface("client.dll", "VClientEntityList003")
local entity_list_raw = ent_list and ffi.cast("void***", ent_list) or nil

local get_client_entity = nil
if entity_list_raw then
    pcall(function()
        get_client_entity = ffi.cast("void*(__thiscall*)(void*,int)", memory.get_vfunc(ent_list, 3))
    end)
end

local model_info_interface = memory.create_interface("engine.dll", "VModelInfoClient004")
local raw_model_info = model_info_interface and ffi.cast("void***", model_info_interface) or nil

local string_table_container = memory.create_interface("engine.dll", "VEngineClientStringTable001")
local raw_string_tables = string_table_container and ffi.cast("void***", string_table_container) or nil

local get_model_index = nil
local find_or_load_model = nil
if raw_model_info then
    pcall(function()
        get_model_index = ffi.cast("int(__thiscall*)(void*, const char*)", memory.get_vfunc(tonumber(ffi.cast("unsigned int", raw_model_info)), 2))
        find_or_load_model = ffi.cast("void(__thiscall*)(void*, const char*)", memory.get_vfunc(tonumber(ffi.cast("unsigned int", raw_model_info)), 43))
    end)
end

local set_model_index_t = ffi.typeof("void(__thiscall*)(void*,int)")

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

local cached_model_indices = {}

local function precache_and_get_index(path)
    if not engine.is_connected() or not engine.is_in_game() then return nil end
    if not path or #path <= 5 then return nil end
    local norm = string.gsub(path, "\\", "/")
    if cached_model_indices[norm] and cached_model_indices[norm] > 0 then
        return cached_model_indices[norm]
    end

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

    if idx and idx > 0 then
        cached_model_indices[norm] = idx
        return idx
    end
    return nil
end

-- UI Player Options list
local model_options = {}
for i, item in ipairs(custom_models_db) do
    table.insert(model_options, item.name)
end

-- Primordial UI Setup
local grp_player = "Model Changer by @saved_messenges"
local ui_ct_model = menu.add_list(grp_player, "CT Custom Model", model_options)
local ui_t_model = menu.add_list(grp_player, "T Custom Model", model_options)

local btn_open_models = menu.add_button(grp_player, "Open Models Folder", function()
    pcall(function()
        if filesystem and filesystem.is_directory and not filesystem.is_directory("csgo/models/player/custom_player") then
            filesystem.create_directory("csgo/models/player/custom_player", "")
        end
        os.execute('cmd /c start "" "csgo\\models\\player\\custom_player"')
        client.log_screen(color_t(0, 255, 180), "[+] Opened custom_player models folder in Explorer")
    end)
end)

local function update_model()
    pcall(function()
        if not engine.is_connected() or not engine.is_in_game() then return end
        local local_player = entity_list.get_local_player()
        if not local_player or not local_player:is_alive() then return end

        local teamnum = local_player:get_prop("m_iTeamNum")
        local selected_idx = 1

        if teamnum == 3 then -- CT
            selected_idx = ui_ct_model:get()
        elseif teamnum == 2 then -- T
            selected_idx = ui_t_model:get()
        end

        if selected_idx and custom_models_db[selected_idx] then
            local selected_item = custom_models_db[selected_idx]
            local model_idx = precache_and_get_index(selected_item.path)
            if not model_idx and selected_item.alt_path then
                model_idx = precache_and_get_index(selected_item.alt_path)
            end

            if model_idx and model_idx > 0 then
                -- 1. Netprop
                local_player:set_prop("m_nModelIndex", model_idx)

                -- 2. Virtual SetModelIndex
                local player_index = local_player:get_index()
                if entity_list_raw and get_client_entity then
                    local player_ptr = ffi.cast("void***", get_client_entity(entity_list_raw, player_index))
                    if player_ptr and player_ptr ~= ffi.NULL and player_ptr[0] ~= nil then
                        local vfunc = memory.get_vfunc(tonumber(ffi.cast("unsigned int", player_ptr)), 75)
                        if vfunc then
                            local set_model_idx = ffi.cast(set_model_index_t, vfunc)
                            if set_model_idx then
                                set_model_idx(player_ptr, model_idx)
                            end
                        end
                    end
                end
            end
        end
    end)
end

callbacks.add(e_callbacks.NET_UPDATE, update_model)
callbacks.add(e_callbacks.PAINT, update_model)
callbacks.add(e_callbacks.EVENT, function() cached_model_indices = {} end, "round_start")
callbacks.add(e_callbacks.EVENT, function() cached_model_indices = {} end, "player_spawn")
callbacks.add(e_callbacks.EVENT, function() cached_model_indices = {} end, "player_team")
