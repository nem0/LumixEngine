inherit "maps/demo/button"

player = Lumix.Entity.NULL

function buttonPressed()
	local doc = this.world.ui:getDocument()
	local elem = doc:getElementByID("runtime_ui")
	elem:setVisible(true)

	this.world.ui:getSystem():enableCursor(true)
	player.lua_script[1].handle_input = false
end
