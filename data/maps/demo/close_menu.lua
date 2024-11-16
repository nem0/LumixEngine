player = {}
Editor.setPropertyType(this, "player", Editor.ENTITY_PROPERTY)

function onButtonClicked()
	this.parent.gui_rect.enabled = false
	this.world:getModule("gui"):getSystem():enableCursor(false)
	player.lua_script[1].handle_input = true
end
