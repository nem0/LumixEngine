### Accessing transform properties (position, rotation, scale)
```lua
-- Position is a table {x, y, z}
local pos = this.position
this.position = {1, 2, 3}

-- Rotation is a quaternion {x, y, z, w}
local rot = this.rotation
this.rotation = {0, 0, 0, 1}  -- identity rotation

-- Scale is a table {x, y, z}
local scl = this.scale
this.scale = {2, 2, 2}
```

### Accessing components and their properties
```lua
-- Access a component on the current entity (e.g., 'this')
local fov = this.camera.fov
this.camera.fov = math.rad(90)  -- in radians

-- Access a component on another entity
local other_entity = this.world:findEntityByName("my_entity")
if other_entity then
	local pos = other_entity.camera.fov
	other_entity.camera.fov = math.rad(45)
end
```

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
			Editor.createEntityEx { -- use only in editor, not accessible ingame, is undoable
			-- this.world:createEntityEx { -- use ingame, is not undoable
				position = { i  * 3, j * 3, k * 3 },
				model_instance = { source = "engine/models/cube.fbx" },
				rigid_actor = { dynamic = 1, box_geometry = { {} } }
			}
		end
	end
end
```

### Destroy entities in editor and ingame
```lua
-- In editor (undoable)
local entity = Editor.createEntity()
-- ... do something with entity
Editor.destroyEntity(entity)

-- Ingame (not undoable)
local entity = this.world:createEntity()
-- ... do something with entity
entity:destroy()
```

### raycastEx
```lua
local hit, entity, hitpos, hitnormal = this.world.physics:raycastEx(this.position, {1, -1, 0})
```

### Raycast from mouse screen coordinates in input handler
```lua
function onInputEvent(event : InputEvent)
	if event.type == "axis" and event.device.type == "mouse" then
		local camera_entity = -- get your camera entity, e.g., this.world:findEntityByName("camera")
		local ray = camera_entity.camera:getRay({event.x_abs, event.y_abs})
		local hit = this.world.renderer:castRay(ray)
		if hit.is_hit then
			-- hit.entity, hit.t, etc.
			black.hAPI.logInfo("Hit entity: " .. tostring(hit.entity))
		end
	end
end
```

### Handling keyboard input
```lua
function onInputEvent(event : InputEvent)
	if event.type == "button" and event.device.type == "keyboard" then
		if event.keycode == black.hAPI.Keycode.W and event.down then
			-- Handle W key press
			black.hAPI.logInfo("W key pressed")
		end
	end
end
```

### Instantiate a prefab
```lua
ext_prefab = black.h.Resource:newEmpty("prefab") -- this is exposed in property grid
local done = false

function update(time_delta)
	if not done then
		local e = this.world:instantiatePrefab({0, 3, 0}, ext_prefab)
		black.hAPI.logInfo("root entity of the instantiated prefab : " .. tostring(e))
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
	black.hAPI.logError("level01 loaded")
end
```

### How to handle array properties
```lua
local entity = Editor.createEntityEx { -- use only in editor, not accessible ingame, is undoable
-- local entity = this.world:createEntityEx { -- use ingame, is not undoable
    position = {0, 0, 0},
    model_instance = { source = "engine/models/sphere.fbx" },
    rigid_actor = { dynamic_type = 1 }
}

entity.rigid_actor.spheres:add()
entity.rigid_actor.spheres[1].radius = 1
```

### GUI button click handler
```lua
	 for i = 1, #tower_types do
        local idx = i
        local button = this.world:createEntityEx({
            gui_button = {},
            gui_rect = {
                left_points = 800,
                top_points = 10 + (idx-1)*70,
                right_points = 950,
                bottom_points = 10 + (idx-1)*70 + 60,
                bottom_relative = 0,
                right_relative = 0,
            },
            gui_text = {
                text = "Tower " .. idx,
                font_size = 30,
                font = "/engine/editor/fonts/notosans-bold.ttf"
            },
            lua_script = {}
        })
        button.parent = canvas
        button.lua_script.scripts:add()
        button.lua_script[1].onButtonClicked = function()
            black.hAPI.logError("clicked " .. tostring(idx))
        end
    end
```
