player = Lumix.Entity.NULL

function start()
	this.world.gui:getSystem():enableCursor(true)
	player.lua_script[1].handle_input = false
end

function update(time_delta)
	local doc = this.world.ui:getDocument()
	local events = doc:getEvents()

	for _, e in ipairs(events) do
		if e.type == LumixAPI.EventType.CLICK then
			local element = doc:getElement(e.element_index)
			if element:getID() == "close" then
				local elem = doc:getElementByID("runtime_ui")
				player.lua_script[1].handle_input = true
				elem:setVisible(false)
			elseif element:getID() == "start" then
				player.lua_script[1].handle_input = true
				doc:getElement(0):setVisible(false)
				this.world.gui:getSystem():enableCursor(false)
			end
		end
	end
end
