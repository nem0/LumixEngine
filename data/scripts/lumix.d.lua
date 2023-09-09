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

declare class spline
end

declare class gui_rect
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

declare class gui_canvas
	orient_to_camera: boolean
	virtual_size: any
end

declare class particle_emitter
	autodestroy: boolean
	source: string
end

declare class terrain
	material: string
	xz_scale: number
	height_scale: number
	tesselation: number
	grid_resolution: number
	grass: any
	getTerrainNormalAt : (terrain, any, any) -> any
	getTerrainHeightAt : (terrain, any, any) -> any
end

declare class camera
	fov: number
	near: number
	far: number
	orthographic: boolean
	orthographic_size: number
end

declare class decal
	material: string
	half_extents: any
	uv_scale: any
end

declare class curve_decal
	material: string
	half_extents: number
	uv_scale: any
	bezier_p0: any
	bezier_p2: any
end

declare class point_light
	cast_shadows: boolean
	dynamic: boolean
	intensity: number
	fov: number
	attenuation: number
	color: any
	range: number
end

declare class environment
	color: any
	intensity: number
	indirect_intensity: number
	shadow_cascades: any
	cast_shadows: boolean
end

declare class instanced_model
	model: string
	blob: any
end

declare class model_instance
	enabled: boolean
	material: string
	source: string
	getModel : (model_instance) -> any
end

declare class environment_probe
	enabled: boolean
	inner_range: any
	outer_range: any
end

declare class reflection_probe
	enabled: boolean
	size: number
	half_extents: any
end

declare class fur
	layers: number
	scale: number
	gravity: number
	enabled: boolean
end

declare class procedural_geom
	material: string
end

declare class bone_attachment
	parent: Entity
	relative_position: any
	relative_rotation: any
	bone: number
end

declare class rigid_actor
	layer: number
	dynamic: number
	trigger: boolean
	box_geometry: any
	sphere_geometry: any
	mesh: string
	material: string
	putToSleep : (rigid_actor) -> any
	getSpeed : (rigid_actor) -> any
	getVelocity : (rigid_actor) -> any
	applyForce : (rigid_actor, any) -> any
	applyImpulse : (rigid_actor, any) -> any
	addForceAtPos : (rigid_actor, any, any) -> any
end

declare class physical_heightfield
	layer: number
	heightmap: string
	y_scale: number
	xz_scale: number
end

declare class physical_controller
	radius: number
	height: number
	layer: number
	use_root_motion: boolean
	use_custom_gravity: boolean
	custom_gravity_acceleration: number
	move : (physical_controller, any) -> any
	isCollisionDown : (physical_controller) -> any
	getGravitySpeed : (physical_controller) -> any
end

declare class js_script
	scripts: any
end

declare class lua_script
	scripts: any
end

declare class gui_image
	enabled: boolean
	color: any
	sprite: string
end

declare class gui_text
	text: string
	font: string
	font_size: number
	horizontal_align: number
	vertical_align: number
	color: any
end

declare class gui_button
	hovered_color: any
	cursor: number
end

declare class gui_render_target
end

declare class animable
	animation: string
end

declare class distance_joint
	connected_body: Entity
	axis_position: any
	damping: number
	stiffness: number
	tolerance: number
	limits: any
end

declare class hinge_joint
	connected_body: Entity
	axis_position: any
	axis_direction: any
	damping: number
	stiffness: number
	use_limit: boolean
	limit: any
end

declare class spherical_joint
	connected_body: Entity
	axis_position: any
	axis_direction: any
	use_limit: boolean
	limit: any
end

declare class d6_joint
	connected_body: Entity
	axis_position: any
	axis_direction: any
	x_motion: number
	y_motion: number
	z_motion: number
	swing_1: number
	swing_2: number
	twist: number
	linear_limit: number
	swing_limit: any
	twist_limit: any
	damping: number
	stiffness: number
	restitution: number
end

declare class vehicle
	speed: number
	current_gear: number
	rpm: number
	mass: number
	center_of_mass: any
	moi_multiplier: number
	chassis: string
	chassis_layer: number
	wheels_layer: number
	setAccel : (vehicle, any) -> any
	setSteer : (vehicle, any) -> any
	setBrake : (vehicle, any) -> any
end

declare class wheel
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

declare class navmesh_agent
	radius: number
	height: number
	move_entity: boolean
	speed: number
	setActive : (navmesh_agent, any) -> any
	navigate : (navmesh_agent, any, any, any) -> any
	cancelNavigation : (navmesh_agent) -> any
	drawPath : (navmesh_agent) -> any
end

declare class navmesh_zone
	extents: any
	agent_height: number
	agent_radius: number
	cell_size: number
	cell_height: number
	walkable_slope_angle: number
	max_climb: number
	autoload: boolean
	detailed: boolean
	load : (navmesh_zone) -> any
	drawContours : (navmesh_zone) -> any
	drawNavmesh : (navmesh_zone, any, any, any, any) -> any
	drawCompactHeightfield : (navmesh_zone) -> any
	drawHeightfield : (navmesh_zone) -> any
	generateNavmesh : (navmesh_zone) -> any
end

declare class script
	script: string
end

declare class lua_script_inline
	code: string
end

declare class gui_input_field
end

declare class property_animator
	animation: string
	enabled: boolean
end

declare class animator
	source: string
	default_set: number
	use_root_motion: boolean
	setFloatInput : (animator, any, any) -> any
	setBoolInput : (animator, any, any) -> any
	getInputIndex : (animator, any) -> any
end

declare class physical_instanced_cube
	half_extents: any
	layer: number
end

declare class physical_instanced_mesh
	mesh: string
	layer: number
end

declare class audio_listener
end

declare class ambient_sound
	sound: string
	pause : (ambient_sound) -> any
	resume : (ambient_sound) -> any
end

declare class echo_zone
	radius: number
	delay__ms_: number
end

declare class chorus_zone
	radius: number
	delay__ms_: number
end



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
	spline: spline
	gui_rect: gui_rect
	gui_canvas: gui_canvas
	particle_emitter: particle_emitter
	terrain: terrain
	camera: camera
	decal: decal
	curve_decal: curve_decal
	point_light: point_light
	environment: environment
	instanced_model: instanced_model
	model_instance: model_instance
	environment_probe: environment_probe
	reflection_probe: reflection_probe
	fur: fur
	procedural_geom: procedural_geom
	bone_attachment: bone_attachment
	rigid_actor: rigid_actor
	physical_heightfield: physical_heightfield
	physical_controller: physical_controller
	js_script: js_script
	lua_script: lua_script
	gui_image: gui_image
	gui_text: gui_text
	gui_button: gui_button
	gui_render_target: gui_render_target
	animable: animable
	distance_joint: distance_joint
	hinge_joint: hinge_joint
	spherical_joint: spherical_joint
	d6_joint: d6_joint
	vehicle: vehicle
	wheel: wheel
	navmesh_agent: navmesh_agent
	navmesh_zone: navmesh_zone
	script: script
	lua_script_inline: lua_script_inline
	gui_input_field: gui_input_field
	property_animator: property_animator
	animator: animator
	physical_instanced_cube: physical_instanced_cube
	physical_instanced_mesh: physical_instanced_mesh
	audio_listener: audio_listener
	ambient_sound: ambient_sound
	echo_zone: echo_zone
	chorus_zone: chorus_zone

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

