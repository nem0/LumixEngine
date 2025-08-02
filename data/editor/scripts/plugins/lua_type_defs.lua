local tpl = [[
export type Vec2 = {number}
export type Vec3 = {number}
export type Color = {number}
export type Quat = {number}
export type DVec3 = {number}
declare ImGui: {
	AlignTextToFramePadding : () -> (),
	Begin : (string, boolean?) -> (boolean, boolean?),
	BeginChildFrame : (string, number, number) -> boolean,
	BeginMenu : (string, boolean) -> boolean,
	BeginPopup : (string) -> boolean,
	Button : (string) -> boolean,
	CalcTextSize : (string) -> (number, number),
	Checkbox : (string, boolean) -> (boolean, boolean),
	CloseCurrentPopup : () -> (),
	CollapsingHeader : (string) -> boolean,
	Columns : (number) -> (),
	DragFloat : (string, number) -> (boolean, number),
	DragInt : (string, number) -> (boolean, number),
	Dummy : (number, number) -> (),
	End : () -> (),
	EndChildFrame : () -> (),
	EndCombo : () -> (),
	EndMenu : () -> (),
	EndPopup : () -> (),
	GetColumnWidth : (number) -> number,
	GetDisplayWidth : () -> number,
	GetDisplayHeight : () -> number,
	GetOsImePosRequest : () -> (number, number),
	GetWindowWidth : () -> (),
	GetWindowHeight : () -> (),
	GetWindowPos : () -> any,
	Indent : (number) -> (),
	InputTextMultiline : (string, string) -> (boolean, string?),
	InputTextMultilineWithCallback : (string, string, (string, number, boolean) -> ()) -> (boolean, string?),
	IsItemHovered : () -> boolean,
	IsKeyPressed : (number, boolean) -> boolean,
	IsMouseClicked : (number) -> boolean,
	IsMouseDown : (number) -> boolean,
	LabelText : (string, string) -> (),
	NewLine : () -> (),
	NextColumn : () -> (),
	OpenPopup : (string) -> (),
	PlotLines : (string, {number}, Vec2) -> (),
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
	SetKeyboardFocusHere : (number) -> (),
	SetNextWindowPos : (number, number) -> (),
	SetNextWindowPosCenter : () -> (),
	SetNextWindowSize : (number, number) -> (),
	SetStyleColor : (number, any) -> (),
	SliderFloat : (string, number, number, number) -> (boolean, number),
	Text : (string) -> (),
	Unindent : (number) -> (),

	Key_DownArrow : number,
	Key_Enter : number,
	Key_Escape : number,
	Key_UpArrow : number
}

declare class World
	getActivePartition : (World) -> number
	setActivePartition : (World, number) -> ()
	createPartition : (World, string) -> number
	load : (World, string, any) -> ()
	getModule : (string) -> any,
	createEntity : () -> Entity,
	createEntityEx : (any) -> Entity,
	findEntityByName : (string) -> Entity?
%s
end

%s

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
%s
end

declare class Resource
	getPath : () -> string
end

declare this:Entity

type ActionDesc = {
	name : string,
	label : string,
	run : () -> ()
}

declare Editor: {
	RESOURCE_PROPERTY : number,
	COLOR_PROPERTY : number,
	ENTITY_PROPERTY : number,
	BOOLEAN_PROPERTY : number,
	setPropertyType : (any, string, number, string?) -> (),
	setArrayPropertyType : (any, string, number, string?) -> (),
	getSelectedEntitiesCount : () -> number,
	getSelectedEntity : (number) -> Entity,
	addAction : (ActionDesc) -> (),
	createEntityEx : (any) -> Entity,
	scene_view : SceneView,
	asset_browser : AssetBrowser
}

declare LumixAPI: {
%s
	INPUT_KEYCODE_SHIFT: number,
	INPUT_KEYCODE_LEFT : number,
	INPUT_KEYCODE_RIGHT : number,
	engine : any,
	logError : (string) -> (),
	logInfo : (string) -> (),
	loadResource : (any, path:string, restype:string) -> any,
	writeFile : (string, string) -> boolean
}

declare class ComponentBase
end

declare class PropertyBase
end

declare class FunctionBase
end

declare class StructVarBase
end

declare class StructBase
end

declare class ModuleReflection
end

declare LumixReflection: {
	getComponent : (number) -> ComponentBase,
	getComponentName : (ComponentBase) -> string,
	getNumComponents : () -> number,
	getNumProperties : (ComponentBase) -> number,
	getNumComponentFunctions : (ComponentBase) -> number,
	getProperty : (ComponentBase, number) -> PropertyBase,
	getComponentFunction : (ComponentBase, number) -> FunctionBase,
	getFunctionName : (FunctionBase) -> string,
	getFunctionArgCount : (FunctionBase) -> number,
	getFunctionArgType : (FunctionBase, number) -> string,
	getFunctionReturnType : (FunctionBase) -> string,
	getPropertyType : (PropertyBase) -> number,
	getPropertyName : (PropertyBase) -> string,
	getFirstModule  : () -> ModuleReflection,
	getNextModule  : (ModuleReflection) -> ModuleReflection?,
	getNumModuleFunctions : (ModuleReflection) -> number,
	getModuleFunction : (ModuleReflection, number) -> FunctionBase,
	getModuleName : (ModuleReflection) -> string,
	getNumFunctions : () -> number,
	getFunction : (number) -> FunctionBase,
	getThisTypeName : (FunctionBase) -> string,
	getReturnTypeName : (FunctionBase) -> string,
	getNumStructs : () -> number,
	getStruct : (number) -> StructBase,
	getStructName : (StructBase) -> string,
	getNumStructMembers : (StructBase) -> number,
	getStructMember : (StructBase, number) -> StructVarBase,
	getStructMemberName : (StructVarBase) -> string,
	getStructMemberType : (StructVarBase) -> number
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
	local objs = {}

	function typeToString(type : number) : string
		if type < 3 then return "number" end
		if type == 3 then return "Entity" end
		if type == 5 then return "Vec3" end
		if type == 8 then return "string" end
		if type == 9 then return "boolean" end
		if type == 10 then return "string" end
		return "any"
	end

	function toLuaIdentifier(v : string)
		local s = string.gsub(v, "[^a-zA-Z0-9]", "_")
		return string.lower(s)
	end

	function writeFuncDecl(code : string, self_type : string, func : FunctionBase, is_component_fn : boolean)
		local func_name = LumixReflection.getFunctionName(func)
		local arg_count = LumixReflection.getFunctionArgCount(func)
		local ret_type = LumixReflection.getFunctionReturnType(func)
		code = code .. `\t{func_name} : ({self_type}`
		local from_arg = if is_component_fn then 2 else 1
		for i = from_arg, arg_count do
			code = code .. ", " .. toLuaType(LumixReflection.getFunctionArgType(func, i - 1))
		end
		code = code .. `) -> {toLuaType(ret_type)}\n`
		return code
	end

	function toLuaTypeName(name : string)
		local t = name:match(".*::(%w*)")
		if t == nil or t:len() == 0 then return name end
		return t
	end

	function toLuaType(ctype : string)
		if string.match(ctype, "^struct Lumix::") then
			ctype = string.sub(ctype, 15)
		end
		if ctype == "int" then return "number" end
		if ctype == "const char *" then return "string" end
		if ctype == "const char*" then return "string" end
		if ctype == "char const *" then return "string" end
		if ctype == "Vec3" then return "Vec3" end
		if ctype == "Quat" then return "Quat" end
		if ctype == "Vec2" then return "Vec2" end
		if ctype == "Color" then return "Color" end
		if ctype == "DVec3" then return "DVec3" end
		if ctype == "EntityPtr" then return "Entity?" end
		if ctype == "EntityRef" then return "Entity" end
		if ctype == "Path" then return "string" end
		if ctype == "i32" then return "number" end
		if ctype == "u32" then return "number" end
		if ctype == "float" then return "number" end
		if ctype == "bool" then return "boolean" end
		if ctype == "void" then return "()" end
		local tmp = toLuaTypeName(ctype)
		if objs[tmp] ~= nil then return tmp end
		return `any --[[{ctype}]]`
	end

	function memberTypeToString(type : number) : string
		if type == 2 then return "boolean" end
		if type == 3 then return "number" end
		if type == 4 then return "number" end
		if type == 5 then return "number" end
		if type == 6 then return "string" end
		if type == 7 then return "Entity" end
		if type == 9 then return "Vec3" end
		return "any"
	end

	function refl()
		local out = ""
		local lumixAPI_src = ""
		local num_structs = LumixReflection.getNumStructs()
		for i = 0, num_structs - 1 do
			local struct = LumixReflection.getStruct(i)
			local name = LumixReflection.getStructName(struct)
			out = out .. `declare class {name}\n`
			local num_members = LumixReflection.getNumStructMembers(struct)
			lumixAPI_src = lumixAPI_src .. `\t{name} : \{ create : () -> {name}, destroy : ({name}) -> () \},\n`
			for j = 0, num_members - 1 do
				local member = LumixReflection.getStructMember(struct, j)
				local member_name = LumixReflection.getStructMemberName(member)
				local type = LumixReflection.getStructMemberType(member)
				out = out .. `\t{member_name} : {memberTypeToString(type)}\n`
			end
			out = out .. `end\n\n`
		end

		local num_funcs = LumixReflection.getNumFunctions()
		for i = 1, num_funcs do
			local fn = LumixReflection.getFunction(i - 1)
			local this_type_name = toLuaTypeName(LumixReflection.getThisTypeName(fn)) 
			if objs[this_type_name] == nil then
				objs[this_type_name] = {}
			end
			table.insert(objs[this_type_name], fn)
		end

		for k, t in pairs(objs) do
			out = out .. `declare class {k}\n`
			for _, fn in ipairs(t) do
				out = writeFuncDecl(out, k, fn, false)
			end
			out = out .. `end\n\n`
		end

		local world_src = ""
		local module = LumixReflection.getFirstModule()
		while module ~= nil do
			local module_name = LumixReflection.getModuleName(module)
			out = out .. `declare class {module_name}_module\n`
			local num_fn = LumixReflection.getNumModuleFunctions(module)
			for i = 1, num_fn do
				local fn = LumixReflection.getModuleFunction(module, i - 1)
				out = writeFuncDecl(out, module_name .. "_module", fn, false)
			end
			out = out .. "end\n\n"
			world_src = world_src .. `\t{module_name} : {module_name}_module\n`
			module = LumixReflection.getNextModule(module)
		end

		local num_cmp = LumixReflection.getNumComponents()
		local entity_src = ""
		for i = 1, num_cmp do
			local cmp = LumixReflection.getComponent(i - 1)
			local name = LumixReflection.getComponentName(cmp)
			local num_props = LumixReflection.getNumProperties(cmp)
			out = out .. `declare class {name}_component\n`
			entity_src = entity_src .. `\t{name}: {name}_component\n`
			for j = 1, num_props do
				local prop = LumixReflection.getProperty(cmp, j - 1)
				local prop_name = LumixReflection.getPropertyName(prop)
				local prop_type = LumixReflection.getPropertyType(prop)
				if prop_name:match("[0-9].*") then continue end
				out = out .. "\t" .. toLuaIdentifier(prop_name) .. ": " .. typeToString(prop_type) .. "\n"
			end
			local num_cmp_funcs = LumixReflection.getNumComponentFunctions(cmp)
			for j = 1, num_cmp_funcs do
				local func = LumixReflection.getComponentFunction(cmp, j - 1)
				out = writeFuncDecl(out, name .. "_component", func, true)
			end
			out = out .. "end\n\n"
		end

		return string.format(tpl, world_src, out, entity_src, lumixAPI_src)
	end

local type_defs = refl()

if false then
	return {
		name = "Lua type defs",
		gui = function()
			if ImGui.Begin("Lua type definitions") then
				ImGui.InputTextMultiline("##code", type_defs)
			end
			ImGui.End()
		end
	}
end