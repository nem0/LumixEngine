# Engine API

The API can be found in [lumix.d.lua](../../data/scripts/lumix.d.lua).

## Safety 

Lua API is almost as unsafe as C++ API and most calls are not not checked. It means that calling method on invalid objects or accessing properties on those objects can crash or overwrite random memory.

```lua
	local e = getSomeEntity()
	...
	e.position = { 1, 2, 3 } -- this is invalid if `e` is destroyed 
```

## Entity

```lua
declare class Entity 
	world : World
	name : string
	parent : Entity?
	rotation : any
	position : Vec3
	scale : Vec3
	hasComponent : (Entity, any) -> boolean
	getComponent : (Entity, any) -> any
	destroy : (Entity) -> ()
	createComponent : (Entity, any) -> any

	-- all components

end
```

### `this`

When inside an entity's script, the `this` variable can be used to access the current entity:

```lua
this.position = { 1, 2, 3 }
```

### Components

Each component is automatically exposed as a member of `Entity`:

```lua
this.gui_rect.enabled = false
```

Component methods are also available:

```lua
local speed_input_idx = this.animator:getInputIndex("speed_y")
```

To add a new item to an array property, use the `add` method:

```lua 
this.lua_script.scripts:add()
```

## World

```lua
declare class World
	getActivePartition : (World) -> number
	setActivePartition : (World, number) -> ()
	createPartition : (World, string) -> number
	load : (World, string, any) -> ()
	getModule : (string) -> any,
	createEntity : () -> Entity,
	createEntityEx : (any) -> Entity,
	findEntityByName : (string) -> Entity?

	... all modules
end
```

World can be accessed through `Entity`. Each module is automatically exposed in `World`.

```lua
local gui = this.world:getModule("gui")
```

```lua
local new_entity = this.world:createEntity()
new_entity.position = { 10, 10, 10 }
```

## Logging

```lua
declare LumixAPI: {
	logError : (string) -> (),
	logInfo : (string) -> (),
}
```

```lua
LumixAPI.logError("Hello world")
```