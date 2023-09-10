local tpl = [[
declare ImGui: {
    AlignTextToFramePadding : () -> (),
    Begin : (string) -> boolean,
    BeginChildFrame : (string, number, number) -> boolean,
    BeginPopup : (string) -> boolean,
    Button : (string) -> boolean,
    CalcTextSize : (string) -> (number, number),
    Checkbox : (string, boolean) -> (boolean, boolean),
    CollapsingHeader : (string) -> boolean,
    Columns : (number) -> (),
    DragFloat : (string, number) -> (boolean, number),
    DragInt : (string, number) -> (boolean, number),
    Dummy : (number, number) -> (),
    End : () -> (),
    EndChildFrame : () -> (),
    EndCombo : () -> (),
    EndPopup : () -> (),
    GetColumnWidth : (number) -> number,
    GetDisplayWidth : () -> number,
    GetDisplayHeight : () -> number,
    GetWindowWidth : () -> (),
    GetWindowHeight : () -> (),
    GetWindowPos : () -> any,
    Indent : (number) -> (),
    InputTextMultiline : (string, string) -> (boolean, string),
    IsItemHovered : () -> boolean,
    IsMouseClicked : (number) -> boolean,
    IsMouseDown : (number) -> boolean,
    LabelText : (string, string) -> (),
    NewLine : () -> (),
    NextColumn : () -> (),
    OpenPopup : (string) -> (),
    PopItemWidth : () -> (),
    PopID : () -> (),
    PopStyleColor : (number) -> (),
    PopStyleVar : (number) -> (),
    PopItemWidth : () -> (),
    PushItemWidth : (number) -> (),
    PushID : (number) -> (),
    PushStyleColor : (number, any) -> (),
    PushStyleVar : (number, number, number) -> () | (number, number) -> () ,
    Rect : (number, number, number) -> (),
    SameLine : () -> (),
    Selectable : (string, boolean) -> boolean | (string) -> boolean,
    Separator : () -> (),
    SetCursorScreenPos : (number, number) -> (),
    SetNextWindowPos : (number, number) -> (),
    SetNextWindowPosCenter : () -> (),
    SetNextWindowSize : (number, number) -> (),
    SetStyleColor : (number, any) -> (),
    SliderFloat : (string, number, number, number) -> (boolean, number),
    Text : (string) -> (),
    Unindent : (number) -> (),
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
    getFunctionArgType : (FunctionBase, number) -> string,
    getFunctionReturnType : (FunctionBase) -> string,
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
if false then
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

    function toLuaType(ctype : string)
        if ctype == "float" then return "number" end
        if ctype == "bool" then return "boolean" end
        if ctype == "void" then return "()" end
        return "any"
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
                if prop_name:match("[0-9].*") then continue end
                out = out .. "\t" .. toLuaIdentifier(prop_name) .. ": " .. typeToString(prop_type) .. "\n"
            end
            local num_funcs = LumixReflection.getNumFunctions(cmp)
            for j = 1, num_funcs do
                local func = LumixReflection.getFunction(cmp, j - 1)
                local func_name = LumixReflection.getFunctionName(func)
                local arg_count = LumixReflection.getFunctionArgCount(func)
                local ret_type = LumixReflection.getFunctionReturnType(func)
                out = out .. `\t{func_name} : ({name}`
                for i = 2, arg_count do
                    out = out .. ", " .. toLuaType(LumixReflection.getFunctionArgType(func, i - 1))
                end
                out = out .. `) -> {toLuaType(ret_type)}\n`
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

