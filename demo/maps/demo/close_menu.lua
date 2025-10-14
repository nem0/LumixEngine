player = Lumix.Entity.NULL

function onButtonClicked()
	this.parent.gui_rect.enabled = false
	this.world:getModule("gui"):getSystem():enableCursor(false)
	player.lua_script[1].handle_input = true
end
