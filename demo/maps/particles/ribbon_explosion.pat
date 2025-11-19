const SPACING = 1;

global attractor : float3

emitter ribbon {
	material "/maps/particles/ribbon.mat"
	init_emit_count 0
	emit_per_second 0
	max_ribbons 1
	max_ribbon_length 400
	init_ribbons_count 0

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_emission : float

	in in_pos : float3

	var pos : float3
	var t : float

	fn output() {
		i_position = pos;
		i_scale = 0.07;
		i_color = {1, 0, 0, 1};
		i_emission = 10;
	}

	fn emit() {
		t = 0;
		pos = in_pos;
	}

	fn update() {
		t = t + time_delta;
	}
}

emitter head {
	material "/maps/particles/explosion.mat"
	init_emit_count 1
	
	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var t : float

	fn update() {
		t = t + time_delta;
		if t > 1 {
			emit(ribbon) {
				in_pos.x = cos(t) * 5;
				in_pos.y = 1;
				in_pos.z = sin(t) * 5;
			};
		}
	}

	fn output() {
		i_position.x = cos(t) * 5;
		i_position.y = 1;
		i_position.z = sin(t) * 5;
		i_scale = 0.5;
		i_color = {1, 0, 1, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 10;
	}

	fn emit() {
		t = 0;
	}
}


