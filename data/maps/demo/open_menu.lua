inherit "maps/demo/button"

gui = {}
player = {}
Editor.setPropertyType(this, "gui", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "player", Editor.ENTITY_PROPERTY)

function buttonPressed()
	gui.gui_rect.enabled = true
	this.world:getModule("gui"):getSystem():enableCursor(true)
	player.lua_script[1].handle_input = false
end
