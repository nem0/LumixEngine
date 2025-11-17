const hspread = 10.5;

emitter ribbon {
	material "/maps/fireworks/ribbon.mat"
	init_emit_count 40
	emit_per_second 0
	max_ribbons 1
	max_ribbon_length 40
	init_ribbons_count 1

	// TODO
		// emit on move / do not emit with the same position
		// docs
		// emit_ribbon()
		// autocomplete
		// kill in ribbons?
		// include
		// mesh, splines, terrains, ...
		// demo
		// fix show/hide preview
		// debugger
		// preview for world space moving ribbons
		// flow control (if, while, for)
		// create mesh from script?
		// saturate, floor, round
		// user function calling another function

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_emission : float

	var pos : float3
	var t : float
	
	fn output() {
		i_position.y = pos.y;
		i_position.z = pos.z;
		i_position.x = pos.x + noise(pos.y * 2.2) * 0.5;
		i_scale = 0.1;
		i_color = {1, 0, 0, 1};
		i_emission = 1;
	}

	fn emit() {
		t = 0;
		pos.x = 0;
		pos.y = emit_index * 0.5;
		pos.z = 0;
	}

	fn update() {
		t = t + time_delta;
	}
}














