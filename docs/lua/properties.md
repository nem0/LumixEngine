# Properties

Properties are non-local variables in scripts. They are visible in the property grid in the editor and can be accessed from outside their owning script. They are also serialized and deserialized with the component itself. Properties can have different types, e.g.:
* string
* float
* i32
* boolean
* entity
* resource

See `Property::Type` in [lua_script_system.h](../../src/lua/lua_script_system.h) for the list of all possible types.

```lua
-- some script.lua
local var = 123 -- this is not visible outside this script - not in property grid, nor is it (de)serialized

-- following variables are properties, i.e., they are editable in property grid, etc.
a = 123
b = "foo"
c = true
d = Lumix.Entity.INVALID
```

## Resource

```lua
-- first parameter, -1, means it's a null resource, same as nullptr in C++
-- second parameter defines the resource type, same as ResourceType in C++ API
res = Lumix.Resource:new(-1, "clip")
```

