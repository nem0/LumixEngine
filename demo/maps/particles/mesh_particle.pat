const horizontal_spread_from = 0.8;
const horizontal_spread_to = 1.2;
const start_vertical_velocity = 5;
const G = 9.8;

emitter Emitter0 {
	mesh "/engine/models/sphere.fbx"
	init_emit_count 0
	emit_per_second 300
	
	out i_rot_lod : float4
	out i_pos_scale : float4

	var pos : float3
	var vel : float3
	var t : float

	fn update() {
		t = t + time_delta;
		pos = pos + vel * time_delta;
		vel.y = vel.y - G * time_delta;
		kill(pos.y < -0.01);
	}

	fn emit() {
		pos = {0, 0, 0};
		let angle = random(0, 2 * 3.14159265);
		let h = random(horizontal_spread_from, horizontal_spread_to);
		vel.x = cos(angle) * h;
		vel.y = start_vertical_velocity;
		vel.z = sin(angle) * h;
		t = 0;
	}

	fn output() {
		i_pos_scale.xyz = pos;
		i_pos_scale.w = min(pos.y * 0.1, 0.1);
		i_rot_lod = {0.707, 0, 0.707, 0};
	}
}
		
