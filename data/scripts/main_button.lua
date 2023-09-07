local loaded = false
function onInputEvent(event)
    if not loaded and event.type == LumixAPI.INPUT_EVENT_BUTTON then
        if event.device.type == LumixAPI.INPUT_DEVICE_KEYBOARD then
			local old_partition = this.world:getActivePartition()
			local demo = this.world:createPartition("demo")
			this.world:setActivePartition(demo)
			this.world:load("demo", function() 
				--this.world:destroyPartition(old_partition)
			end)

			loaded = true
			this.gui_rect.enabled = false
		end
	end
end
