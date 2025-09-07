-- open the lua script of the selected entity in the editor
Editor.addAction {
	name = "open_lua_script", 
	label = "Open Lua Script",
	run = function()
		if Editor.getSelectedEntitiesCount() ~= 1 then return end
		local e = Editor.getSelectedEntity(0)
		if e.lua_script == nil then return end
		if e.lua_script.scripts[1] == nil then return end
		local script_path = e.lua_script.scripts[1].path
		Editor.asset_browser:openEditor(script_path)
	end
} 
