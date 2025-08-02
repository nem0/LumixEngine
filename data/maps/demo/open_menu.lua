inherit "maps/demo/button"

gui = Lumix.Entity.NULL
player = Lumix.Entity.NULL

function buttonPressed()
	gui.gui_rect.enabled = true
	this.world:getModule("gui"):getSystem():enableCursor(true)
	player.lua_script[1].handle_input = false
end
