--[[
{
    "luau-lsp.types.definitionFiles": ["scripts/lumix.d.lua"],
    "luau-lsp.types.roblox": false
}
]]--

declare ImGui: {
    Begin : (string) -> boolean,
    End : () -> (),
    Text : (string) -> ()
}

declare class World
    getActivePartition : (World) -> number
    setActivePartition : (World, number) -> ()
    createPartition : (World, string) -> number
    load : (World, string, any) -> ()
end

declare class GUIRect
    enabled : boolean
end

declare class NavmeshAgent
    navigate : (NavmeshAgent, any, number, number) -> ()
end

declare class PhysicalController
    getGravitySpeed : (PhysicalController) -> number
    move : (PhysicalController, any) -> ()
end

declare class Animator
    getInputIndex : (Animator, string) -> number
    setFloatInput : (Animator, number, number) -> ()
    setBoolInput : (Animator, number, boolean) -> ()
end

declare class Entity 
    world : World
    animator : Animator
    gui_rect : GUIRect
    navmesh_agent : NavmeshAgent
    physical_controller : PhysicalController
    rotation : any
    position : any
    scale : any
end


declare this:Entity

declare Editor: {
    ENTITY_PROPERTY : number,
    setPropertyType : (any, string, number) -> ()
}

declare LumixAPI: {
    INPUT_KEYCODE_SHIFT: number,
    INPUT_KEYCODE_LEFT : number,
    INPUT_KEYCODE_RIGHT : number,
    logError : (string) -> (),
    logInfo : (string) -> ()
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
