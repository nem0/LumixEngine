inherit "maps/demo/button"

spawn_point = {}
point0 = {}
point1 = {}
point2 = {}
point3 = {}
Editor.setPropertyType(this, "spawn_point", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point0", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point1", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point2", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point3", Editor.ENTITY_PROPERTY)

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
