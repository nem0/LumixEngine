const SPACING = 1;

global attractor : float3

emitter ribbon {
	material "/maps/particles/ribbon.mat"
	init_emit_count 40
	emit_per_second 0
	max_ribbons 100
	max_ribbon_length 40
	init_ribbons_count 100

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_emission : float

	var pos : float3
	var t : float

	fn output() {
		i_position.z = pos.z
			+ noise(pos.y * 1.3 + pos.z * pos.x + t) * 0.3
			- (min(1, (pos.z - attractor.z) * 0.1)) * pos.y * pos.y * 0.4
			;
		i_position.x = pos.x
			+ noise(pos.y * 1.1 + pos.z + pos.x + t) * 0.3
			- (min(1, (pos.x - attractor.x) * 0.1)) * pos.y * pos.y * 0.4
			;
		i_position.y = pos.y;
		i_scale = 0.04;//(1 - pos.y * 0.05) * 0.1;
		i_color = {1, 0, 0, 1};
		i_emission = 10;
	}

	fn emit() {
		t = 0;
		pos.y = emit_index * 0.08 * SPACING;
		pos.x = (ribbon_index / 10) * SPACING;
		pos.z = (ribbon_index % 10) * SPACING;
	}

	fn update() {
		t = t + time_delta;
	}
}


