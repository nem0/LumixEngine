inherit "maps/demo/button"

spawn_point = {}
Editor.setPropertyType(this, "spawn_point", Editor.ENTITY_PROPERTY)

function buttonPressed()
	local e = this.world:createEntityEx {
		position = spawn_point.position,
		scale = {0.3, 1.5, 0.3},
		navmesh_agent = {},
		lua_script = {
		--	path = "data/scripts/nav_agent.lua"
		},
		model_instance = {
			source = "models/shapes/cylinder.fbx"
		}

	}

end