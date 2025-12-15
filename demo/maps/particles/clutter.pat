global effector_pos : float3

import "engine/particles/common.pai"

emitter Emitter0 {
	material "/maps/particles/world_space_particle.mat"
	init_emit_count 900
	
	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var pos : float3

	fn update() {}

	fn emit() {
		pos.x = entity_position.x + (emit_index % 30) * 0.6;
		pos.y = entity_position.y + 0.1;
		pos.z = entity_position.z + (emit_index / 30) * 0.6;
	}
	fn output() {
		i_position.xz = pos.xz;
		i_position.y = pos.y + saturate(1 - distance(pos, effector_pos) * 0.3);
		i_scale = 0.1;
		i_color = {1, 0, 0, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 10;
	}
}
		
