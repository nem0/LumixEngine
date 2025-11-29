function start() 
	for i = 1,30 do 
		for j = 1,10 do 
			local ybot = this.world:createEntityEx {
				position = {i * 2, 0, j * 2},
				model_instance = { source = "models/ybot/ybot.fbx" },
				lua_script = {},
				navmesh_agent = {  },
				animator = { source = "models/ybot/ybot.act" }
			}
			ybot.lua_script.scripts:add()
			ybot.lua_script.scripts[1].path = "maps/navigation_stress_test/bot2.lua"
		end
	end
end
