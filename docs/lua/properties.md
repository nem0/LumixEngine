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
d = Lumix.Entity.NULL -- d point to no entity, but we know the type of `d` is Entity
```

## Resource

```lua
-- the parameter defines the resource type, same as ResourceType in C++ API
res = Lumix.Resource:newEmpty("clip")
```

## Arrays

Arrays can be exposed using `Editor.setArrayPropertyType`. Using `Editor.setArrayPropertyType` is necessary because with it we don't know what type are array's elements. 

```lua
test = {}
Editor.setArrayPropertyType(this, "test", Editor.FLOAT_PROPERTY)

test2 = {}
Editor.setArrayPropertyType(this, "test2", Editor.ENTITY_PROPERTY)

test3 = {}
Editor.setArrayPropertyType(this, "test3", Editor.INT_PROPERTY)

test4 = {}
Editor.setArrayPropertyType(this, "test4", Editor.BOOLEAN_PROPERTY)

test5 = {}
Editor.setArrayPropertyType(this, "test5", Editor.COLOR_PROPERTY)

test6 = {}
Editor.setArrayPropertyType(this, "test6", Editor.RESOURCE_PROPERTY, "model")

unknown_array = {} -- without Editor.setArrayPropertyType, we have no idea what is supposed to be stored in this array

```