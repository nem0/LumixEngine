local tpl = [[
declare ImGui: {
    Begin : (string) -> boolean,
    End : () -> (),
    Text : (string) -> (),
    Button : (string) -> boolean,
    SameLine : () -> (),
    InputTextMultiline : (string, string) -> (boolean, string)
}

declare class World
    getActivePartition : (World) -> number
    setActivePartition : (World, number) -> ()
    createPartition : (World, string) -> number
    load : (World, string, any) -> ()
end

%s

declare class Entity 
    world : World
    name : string
    parent : Entity?
    rotation : any
    position : any
    scale : any
    hasComponent : (Entity, any) -> boolean
    getComponent : (Entity, any) -> any
    destroy : (Entity) -> ()
    createComponent : (Entity, any) -> any
%s
end


declare this:Entity

declare Editor: {
    ENTITY_PROPERTY : number,
    BOOLEAN_PROPERTY : number,
    setPropertyType : (any, string, number) -> ()
}

declare LumixAPI: {
    INPUT_KEYCODE_SHIFT: number,
    INPUT_KEYCODE_LEFT : number,
    INPUT_KEYCODE_RIGHT : number,
    logError : (string) -> (),
    logInfo : (string) -> (),
}

declare class ComponentBase
end

declare class PropertyBase
end

declare class FunctionBase
end

declare LumixReflection: {
    getComponent : (number) -> ComponentBase,
    getComponentName : (ComponentBase) -> string,
    getNumComponents : () -> number,
    getNumProperties : (ComponentBase) -> number,
    getNumFunctions : (ComponentBase) -> number,
    getProperty : (ComponentBase, number) -> PropertyBase,
    getFunction : (ComponentBase, number) -> FunctionBase,
    getFunctionName : (FunctionBase) -> string,
    getFunctionArgCount : (FunctionBase) -> number,
    getPropertyType : (PropertyBase) -> number,
    getPropertyName : (PropertyBase) -> string
}


type InputDevice = {
    type : "mouse" | "keyboard",
    index : number
}

type AxisInputEvent = {
    type : "axis",
    device : InputDevice,
    x : number,
    y : number,
    x_abs : number,
    y_abs : number
}

type ButtonInputEvent = {
    type : "button",
    device : InputDevice,
    key_id : number,
    down : boolean,
    is_repeat : boolean,
    x : number,
    y : number
}

export type InputEvent = ButtonInputEvent | AxisInputEvent

]]
if true then
    function typeToString(type : number) : string
        if type < 3 then return "number" end
        if type == 3 then return "Entity" end
        if type == 8 then return "string" end
        if type == 9 then return "boolean" end
        if type == 10 then return "string" end
        return "any"
    end

    function toLuaIdentifier(v : string)
        local s = string.gsub(v, "[^a-zA-Z0-9]", "_")
        return string.lower(s)
    end

    function refl()
        local num_cmp = LumixReflection.getNumComponents()
        local out = ""
        local entity_src = ""
        for i = 1, num_cmp do
            local cmp = LumixReflection.getComponent(i - 1)
            local name = LumixReflection.getComponentName(cmp)
            local num_props = LumixReflection.getNumProperties(cmp)
            out = out .. "declare class " .. name .. "\n"
            entity_src = entity_src .. `\t{name}: {name}\n`
            for j = 1, num_props do
                local prop = LumixReflection.getProperty(cmp, j - 1)
                local prop_name = LumixReflection.getPropertyName(prop)
                local prop_type = LumixReflection.getPropertyType(prop)
                if prop_name:match("3.*") then continue end
                out = out .. "\t" .. toLuaIdentifier(prop_name) .. ": " .. typeToString(prop_type) .. "\n"
            end
            local num_funcs = LumixReflection.getNumFunctions(cmp)
            for j = 1, num_funcs do
                local func = LumixReflection.getFunction(cmp, j - 1)
                local func_name = LumixReflection.getFunctionName(func)
                local arg_count = LumixReflection.getFunctionArgCount(func)
                out = out .. `\t{func_name} : ({name}`
                for i = 2, arg_count do
                    out = out .. ", any"
                end
                out = out .. ") -> any\n"
            end
            out = out .. "end\n\n"
        end

        return string.format(tpl, out, entity_src)
    end

    local type_defs = refl()

    return coroutine.create(function()
        while true do
            if ImGui.Begin("Lua type definitions") then
                ImGui.InputTextMultiline("Types", type_defs)
            end
            ImGui.End()
            coroutine.yield()    
        end
    end)
end