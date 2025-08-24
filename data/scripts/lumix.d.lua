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
	getModule : (string) -> any
	createEntity : () -> Entity
	createEntityEx : (any) -> Entity
	findEntityByName : (string) -> Entity?
	core : core_module
	lua_script : lua_script_module
	gui : gui_module
	navigation : navigation_module
	audio : audio_module
	animation : animation_module
	renderer : renderer_module
	physics : physics_module

end

declare class RaycastHit
	position : Vec3
	normal : Vec3
	entity : Entity
end

declare class SweepHit
	position : Vec3
	normal : Vec3
	distance : number
	entity : Entity
end

declare class Ray
	origin : any
	dir : Vec3
end

declare class RayCastModelHit
	is_hit : boolean
	t : number
	entity : Entity
end

declare class GUISystem
	enableCursor : (GUISystem, boolean) -> ()
end

declare class AssetBrowser
	openEditor : (AssetBrowser, string) -> ()
end

declare class SceneView
	getViewportRotation : (SceneView) -> Quat
	setViewportRotation : (SceneView, Quat) -> ()
	getViewportPosition : (SceneView) -> DVec3
	setViewportPosition : (SceneView, DVec3) -> ()
end

declare class Model
	getBoneCount : (Model) -> number
	getBoneName : (Model, number) -> any --[[char]]
	getBoneParent : (Model, number) -> number
end

declare class core_module
end

declare class lua_script_module
end

declare class gui_module
	getRectAt : (gui_module, Vec2) -> Entity?
	isOver : (gui_module, Vec2, Entity?) -> boolean
	getSystem : (gui_module) -> GUISystem
end

declare class navigation_module
end

declare class audio_module
	setMasterVolume : (audio_module, number) -> ()
	play : (audio_module, Entity?, string, boolean) -> number
	stop : (audio_module, number) -> ()
	isEnd : (audio_module, number) -> boolean
	setFrequency : (audio_module, number, number) -> ()
	setVolume : (audio_module, number, number) -> ()
	setEcho : (audio_module, number, number, number, number, number) -> ()
end

declare class animation_module
end

declare class renderer_module
	addDebugCross : (renderer_module, DVec3, number, Color) -> ()
	addDebugLine : (renderer_module, DVec3, DVec3, Color) -> ()
	addDebugTriangle : (renderer_module, DVec3, DVec3, DVec3, Color) -> ()
	castRay : (renderer_module, any --[[void*]], Entity?) -> any --[[RayCastModelHit]]
	setActiveCamera : (renderer_module, Entity?) -> ()
end

declare class physics_module
	raycast : (physics_module, Vec3, Vec3, number, Entity?) -> Entity?
	raycastEx : (physics_module, Vec3, Vec3, number, any --[[void*]], Entity?, number) -> boolean
	sweepSphere : (physics_module, DVec3, number, Vec3, number, any --[[void*]], Entity?, number) -> boolean
	setGravity : (physics_module, Vec3) -> ()
end

declare class gui_rect_component
	enabled: boolean
	clip_content: boolean
	top_points: number
	top_relative: number
	right_points: number
	right_relative: number
	bottom_points: number
	bottom_relative: number
	left_points: number
	left_relative: number
end

declare class gui_canvas_component
	orient_to_camera: boolean
	virtual_size: any
end

declare class signal_component
	blob: any
end

declare class spline_component
	blob: any
end

declare class model_instance_component
	enabled: boolean
	source: string
	getModel : (model_instance_component) -> Model
	overrideMaterialVec4 : (model_instance_component, number, string, any --[[Vec4]]) -> boolean
end

declare class bone_attachment_component
	parent: Entity
	relative_position: Vec3
	relative_rotation: Vec3
	bone: number
end

declare class physical_controller_component
	radius: number
	height: number
	layer: number
	use_root_motion: boolean
	use_custom_gravity: boolean
	custom_gravity_acceleration: number
	move : (physical_controller_component, Vec3) -> ()
	isCollisionDown : (physical_controller_component) -> boolean
	getGravitySpeed : (physical_controller_component) -> number
end

declare class distance_joint_component
	connected_body: Entity
	axis_position: Vec3
	damping: number
	stiffness: number
	tolerance: number
	limits: any
end

declare class hinge_joint_component
	connected_body: Entity
	axis_position: Vec3
	axis_direction: Vec3
	damping: number
	stiffness: number
	use_limit: boolean
	limit: any
end

declare class spherical_joint_component
	connected_body: Entity
	axis_position: Vec3
	axis_direction: Vec3
	use_limit: boolean
	limit: any
end

declare class d6_joint_component
	connected_body: Entity
	axis_position: Vec3
	axis_direction: Vec3
	x_motion: number
	y_motion: number
	z_motion: number
	twist: number
	linear_limit: number
	swing_limit: any
	twist_limit: any
	damping: number
	stiffness: number
	restitution: number
end

declare class rigid_actor_component
	layer: number
	dynamic: number
	trigger: boolean
	box_geometry: any
	sphere_geometry: any
	ccd: boolean
	mesh: string
	material: string
	putToSleep : (rigid_actor_component) -> ()
	getSpeed : (rigid_actor_component) -> number
	getVelocity : (rigid_actor_component) -> Vec3
	applyForce : (rigid_actor_component, Vec3) -> ()
	applyImpulse : (rigid_actor_component, Vec3) -> ()
	addForceAtPos : (rigid_actor_component, Vec3, Vec3) -> ()
end

declare class vehicle_component
	speed: number
	current_gear: number
	rpm: number
	mass: number
	center_of_mass: Vec3
	moi_multiplier: number
	chassis: string
	chassis_layer: number
	wheels_layer: number
	setAccel : (vehicle_component, number) -> ()
	setSteer : (vehicle_component, number) -> ()
	setBrake : (vehicle_component, number) -> ()
end

declare class wheel_component
	radius: number
	width: number
	mass: number
	moi: number
	max_compression: number
	max_droop: number
	spring_strength: number
	spring_damper_rate: number
	slot: number
	rpm: number
end

declare class particle_emitter_component
	autodestroy: boolean
	source: string
end

declare class terrain_component
	material: string
	xz_scale: number
	height_scale: number
	tesselation: number
	grid_resolution: number
	grass: any
	getTerrainNormalAt : (terrain_component, number, number) -> Vec3
	getTerrainHeightAt : (terrain_component, number, number) -> number
end

declare class camera_component
	fov: number
	near: number
	far: number
	orthographic: boolean
	orthographic_size: number
	film_grain_intensity: number
	dof_enabled: boolean
	dof_distance: number
	dof_range: number
	dof_max_blur_size: number
	dof_sharp_range: number
	bloom_enabled: boolean
	bloom_tonemap_enabled: boolean
	bloom_accomodation_speed: number
	bloom_average_bloom_multiplier: number
	bloom_exposure: number
	getRay : (camera_component, Vec2) -> any --[[Ray]]
end

declare class decal_component
	material: string
	half_extents: Vec3
	uv_scale: any
end

declare class curve_decal_component
	material: string
	half_extents: number
	uv_scale: any
end

declare class point_light_component
	cast_shadows: boolean
	dynamic: boolean
	intensity: number
	fov: number
	attenuation: number
	color: Vec3
	range: number
end

declare class environment_component
	color: Vec3
	intensity: number
	indirect_intensity: number
	shadow_cascades: any
	cast_shadows: boolean
	sky_texture: string
	atmosphere_enabled: boolean
	godrays_enabled: boolean
	clouds_enabled: boolean
	clouds_top: number
	clouds_bottom: number
	sky_intensity: number
	scatter_rayleigh: Vec3
	scatter_mie: Vec3
	absorb_mie: Vec3
	sunlight_color: Vec3
	sunlight_strength: number
	height_distribution_rayleigh: number
	height_distribution_mie: number
	ground_radius: number
	atmosphere_radius: number
	fog_density: number
	fog_scattering: Vec3
	fog_top: number
end

declare class instanced_model_component
	model: string
	blob: any
end

declare class environment_probe_component
	enabled: boolean
	inner_range: Vec3
	outer_range: Vec3
end

declare class reflection_probe_component
	enabled: boolean
	size: number
	half_extents: Vec3
end

declare class fur_component
	layers: number
	scale: number
	gravity: number
	enabled: boolean
end

declare class procedural_geom_component
	material: string
end

declare class animable_component
	animation: string
end

declare class navmesh_agent_component
	radius: number
	height: number
	move_entity: boolean
	speed: number
	setActive : (navmesh_agent_component, boolean) -> ()
	navigate : (navmesh_agent_component, DVec3, number, number) -> boolean
	cancelNavigation : (navmesh_agent_component) -> ()
	drawPath : (navmesh_agent_component, boolean) -> ()
end

declare class navmesh_zone_component
	extents: Vec3
	agent_height: number
	agent_radius: number
	cell_size: number
	cell_height: number
	walkable_slope_angle: number
	max_climb: number
	autoload: boolean
	detailed: boolean
	load : (navmesh_zone_component) -> boolean
	drawContours : (navmesh_zone_component) -> ()
	drawNavmesh : (navmesh_zone_component, DVec3, boolean, boolean, boolean) -> ()
	drawCompactHeightfield : (navmesh_zone_component) -> ()
	drawHeightfield : (navmesh_zone_component) -> ()
	generateNavmesh : (navmesh_zone_component) -> any --[[NavmeshBuildJob]]
end

declare class gui_image_component
	enabled: boolean
	color: any
	sprite: string
end

declare class gui_text_component
	text: string
	font: string
	font_size: number
	horizontal_align: number
	vertical_align: number
	color: any
end

declare class gui_button_component
	hovered_color: any
	cursor: number
end

declare class gui_render_target_component
end

declare class lua_script_component
	scripts: any
	getScriptPath : (lua_script_component, number) -> string
end

declare class property_animator_component
	animation: string
	enabled: boolean
	looped: boolean
end

declare class animator_component
	source: string
	default_set: number
	use_root_motion: boolean
	setFloatInput : (animator_component, number, number) -> ()
	setBoolInput : (animator_component, number, boolean) -> ()
	setVec3Input : (animator_component, number, Vec3) -> ()
	getInputIndex : (animator_component, string) -> number
end

declare class physical_heightfield_component
	layer: number
	heightmap: string
	y_scale: number
	xz_scale: number
end

declare class lua_script_inline_component
	code: string
end

declare class physical_instanced_cube_component
	half_extents: Vec3
	layer: number
end

declare class physical_instanced_mesh_component
	mesh: string
	layer: number
end

declare class audio_listener_component
end

declare class ambient_sound_component
	sound: string
	pause : (ambient_sound_component) -> ()
	resume : (ambient_sound_component) -> ()
end

declare class echo_zone_component
	radius: number
	delay__ms_: number
end

declare class chorus_zone_component
	radius: number
	delay__ms_: number
end

declare class gui_input_field_component
end



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
	gui_rect: gui_rect_component
	gui_canvas: gui_canvas_component
	signal: signal_component
	spline: spline_component
	model_instance: model_instance_component
	bone_attachment: bone_attachment_component
	physical_controller: physical_controller_component
	distance_joint: distance_joint_component
	hinge_joint: hinge_joint_component
	spherical_joint: spherical_joint_component
	d6_joint: d6_joint_component
	rigid_actor: rigid_actor_component
	vehicle: vehicle_component
	wheel: wheel_component
	particle_emitter: particle_emitter_component
	terrain: terrain_component
	camera: camera_component
	decal: decal_component
	curve_decal: curve_decal_component
	point_light: point_light_component
	environment: environment_component
	instanced_model: instanced_model_component
	environment_probe: environment_probe_component
	reflection_probe: reflection_probe_component
	fur: fur_component
	procedural_geom: procedural_geom_component
	animable: animable_component
	navmesh_agent: navmesh_agent_component
	navmesh_zone: navmesh_zone_component
	gui_image: gui_image_component
	gui_text: gui_text_component
	gui_button: gui_button_component
	gui_render_target: gui_render_target_component
	lua_script: lua_script_component
	property_animator: property_animator_component
	animator: animator_component
	physical_heightfield: physical_heightfield_component
	lua_script_inline: lua_script_inline_component
	physical_instanced_cube: physical_instanced_cube_component
	physical_instanced_mesh: physical_instanced_mesh_component
	audio_listener: audio_listener_component
	ambient_sound: ambient_sound_component
	echo_zone: echo_zone_component
	chorus_zone: chorus_zone_component
	gui_input_field: gui_input_field_component

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
	RaycastHit : { create : () -> RaycastHit, destroy : (RaycastHit) -> () },
	SweepHit : { create : () -> SweepHit, destroy : (SweepHit) -> () },
	Ray : { create : () -> Ray, destroy : (Ray) -> () },
	RayCastModelHit : { create : () -> RayCastModelHit, destroy : (RayCastModelHit) -> () },

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

