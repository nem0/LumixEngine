### Show mouse cursor ingame

```lua
function start()
	this.world:getModule("gui"):getSystem():enableCursor(true)
end
```

### Create a grid 7 x 120 x 7 cubes with box physics
```lua
for i = 1, 7 do
	for j = 1, 120 do
		for k = 1, 7 do
			Editor.createEntityEx {
				position = { i  * 3, j * 3, k * 3 },
				model_instance = { source = "models/shapes/cube.fbx" },
				rigid_actor = { dynamic = 1, box_geometry = { {} } }
			}
		end
	end
end
```

### Instantiate a prefab
```lua
ext_prefab = Lumix.Resource:newEmpty("prefab")
local done = false

function update(time_delta)
	if not done then
		local e = this.world:instantiatePrefab({0, 3, 0}, ext_prefab)
		LumixAPI.logInfo("root entity of the instantiated prefab : " .. tostring(e))
		done = true
	end
end
```


### Load another world as a partition of current world

```lua
function start()
	level_partition = this.world:createPartition("level")
	this.world:setActivePartition(level_partition)
	this.world:load("maps/level01.unv", onLevelLoaded)
end

function onLevelLoaded()
	LumixAPI.logError("level01 loaded")
end
```