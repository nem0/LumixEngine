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
			this.world:createEntityEx {
				position = { i  * 3, j * 3, k * 3 },
				model_instance = { source = "models/shapes/cube.fbx" },
				rigid_actor = { dynamic = 1, box_geometry = { {} } }
			}
		end
	end
end
```