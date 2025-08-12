local run_on_entity = false
local suggestions = {}
local open_popup = false
local autocomplete_offset = 0
local focus_request = false
local autocomplete_selected_idx = -1
local insert_text : string? = nil

local plugin = {
	name = "Lua console",
	-- settings are automatically saved/loaded
	settings = {
		lua_console_open = true,
		lua_console_code = ""
	}
}

-- if windowMenuAction exists, menu item is added to "Window" menu and windowMenuAction is called when it's activated
plugin.windowMenuAction = function()
	plugin.settings.is_lua_console_open = not plugin.settings.is_lua_console_open
end

function execute()
	local f, err = loadstring(plugin.settings.lua_console_code)
	if f == nil then
		LumixAPI.logError(err)
	else 
		if run_on_entity then
			local selected_count = Editor.getSelectedEntitiesCount()
			if selected_count == 0 then
				LumixAPI.logError("No entities selected")
			end
			for i = 1, selected_count do
				local entity = Editor.getSelectedEntity(i - 1)
				if entity.lua_script == nil then 
					LumixAPI.logError("Entity does not have lua script")
					continue
				end
				local env = entity.lua_script[1]
				if env ~= nil then
					setfenv(f, env)
					f()
				else
					LumixAPI.logError("Entity does not have lua script")
				end
			end
		else
			f()
		end
	end
end

function callback(text : string, cursor_pos : number, is_completition : boolean)
	if is_completition then
		suggestions = {}
		local from, to = text:reverse():find("[a-zA-Z0-9_\.]*", #text - cursor_pos + 1)
		local w = text:reverse():sub(from, to):reverse()
		local t = _G
		local last = ""
		for k in w:gmatch("%w+") do
			if t[k] ~= nil then t = t[k] end
			last = k
		end
		if t == nil then return nil end

		for k, v in pairs(t) do
			if k:find(`{last}`, 1, true) == 1 then
				table.insert(suggestions, k)
			end
		end
		table.sort(suggestions)
		if #suggestions == 1 then return suggestions[1]:sub(#last + 1) end
		if #suggestions > 1 then 
			open_popup = true
			autocomplete_offset = #last + 1
		end
		return nil
	else
		local t = insert_text
		insert_text = nil
		return t
	end
end

plugin.gui = function()
	local open = plugin.settings.is_lua_console_open
	if not open then return end
	local visible : boolean
	visible, open = ImGui.Begin("Lua console", open)
	plugin.settings.is_lua_console_open = open
	if visible then
		if ImGui.Button("Execute") then
			execute()
		end
		ImGui.SameLine()
		_, run_on_entity = ImGui.Checkbox("Run on entity", run_on_entity)
		if focus_request then
			focus_request = false
			ImGui.SetKeyboardFocusHere(0)
		end

		local changed, new_code = ImGui.InputTextMultilineWithCallback("##code", plugin.settings.lua_console_code, callback)
		if changed then plugin.settings.lua_console_code = new_code end
		if open_popup then
			open_popup = false
			ImGui.OpenPopup("lua_console_autocomplete")
			local x, y = ImGui.GetOsImePosRequest()
			ImGui.SetNextWindowPos(x, y)
		end
		if ImGui.BeginPopup("lua_console_autocomplete") then
			if ImGui.IsKeyPressed(ImGui.Key_UpArrow, true) then autocomplete_selected_idx -= 1 end
			if ImGui.IsKeyPressed(ImGui.Key_DownArrow, true) then autocomplete_selected_idx += 1 end
			if autocomplete_selected_idx < -1 then autocomplete_selected_idx = 0 end
			if autocomplete_selected_idx >= #suggestions then autocomplete_selected_idx = #suggestions - 1 end
			if ImGui.IsKeyPressed(ImGui.Key_Escape, false) then ImGui.CloseCurrentPopup() end
			if ImGui.IsKeyPressed(ImGui.Key_Enter, false) then
				ImGui.CloseCurrentPopup()
				local k = suggestions[autocomplete_selected_idx + 1]
				autocomplete_selected_idx = -1
				insert_text = k:sub(autocomplete_offset)
				focus_request = true
			else 
				for i, k in ipairs(suggestions) do

					if ImGui.Selectable(k, autocomplete_selected_idx == i - 1) then
						insert_text = k:sub(autocomplete_offset)
						focus_request = true
						autocomplete_selected_idx = -1
					end
				end
			end
			ImGui.EndPopup()
		end
	end
	ImGui.End()
end

return plugin
