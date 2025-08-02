inherit "maps/demo/button"

spawn_point = Lumix.Entity.NULL
point0 = Lumix.Entity.NULL
point1 = Lumix.Entity.NULL
point2 = Lumix.Entity.NULL
point3 = Lumix.Entity.NULL

function buttonPressed()
	local e : Entity = this.world:createEntityEx {
		position = spawn_point.position,
		scale = {0.3, 1.5, 0.3},
		navmesh_agent = {},
		lua_script = {},
		model_instance = {
			source = "models/shapes/cylinder.fbx"
		}
	}
	e.lua_script.scripts.add()
	e.lua_script.scripts[1].path = "maps/demo/random_walk_ai.lua"
	local env = e.lua_script[1]
	env.point0 = point0
	env.point1 = point1
	env.point2 = point2
	env.point3 = point3
end
