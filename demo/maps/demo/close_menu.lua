player = Lumix.Entity.NULL

function onButtonClicked()
	this.parent.gui_rect.enabled = false
	this.world.ui:getSystem():enableCursor(false)
	player.lua_script[1].handle_input = true
end
