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

declare this:any
declare Editor:any

declare LumixAPI: {
    INPUT_KEYCODE_SHIFT: number
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