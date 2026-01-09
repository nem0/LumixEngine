import "/engine/particles/common.pai"

emitter Emitter0 {
	material "/maps/particles/world_space_particle.mat"
	init_emit_count 0
	emit_per_second 100
	
	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var vel : float3
	var pos : float3
	var t : float

	fn update() {
		t = t + time_delta;
		pos = pos + vel * time_delta;
		if t > 1 {
			kill();
		}
	}
	fn emit() {
		pos = entity_position;
		vel.x = random(-1, 1);
		vel.z = random(-1, 1);
		vel.y = 2;
		t = 0;
	}

	fn output() {
		i_position = pos;
		i_scale = 0.1;
		i_color = {1, 0, 0, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 10;
	}
}

emitter Ribbon0 {
	material "/maps/particles/world_space_ribbon.mat"
	max_ribbons 1
	init_ribbons_count 1
	max_ribbon_length 30
	emit_move_distance 0.03

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_emission : float
	
	var pos : float3
	var t : float

	fn output() {
		i_position = pos;
		i_scale = 0.1;
		i_color = {1, 0, 0, t};
		i_emission = 10;
	}

	fn emit() {
		t = 1;
		pos = entity_position;
	}

	fn update() {
		t = t - time_delta * 0.7;
		t = max(t, 0);
	}
}
		
