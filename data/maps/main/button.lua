local loaded = false
function onInputEvent(event : InputEvent)
    if not loaded and event.type == "button" then
        if event.device.type == "keyboard" then
			local old_partition = this.world:getActivePartition()
			local demo = this.world:createPartition("demo")
			this.world:setActivePartition(demo)
			this.world:load("maps/demo/demo.unv", function() 
				--this.world:destroyPartition(old_partition)
			end)

			loaded = true
			this.gui_rect.enabled = false
		end
	end
end
