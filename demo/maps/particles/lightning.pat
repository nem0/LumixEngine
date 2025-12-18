const JITTER_SPEED = 30;
const JITTER_SIZE = 0.05;

import "engine/particles/common.pai"
global g_target : float3

fn mix(a, b, t) {
	return a + (b - a) * t;
}

emitter ribbon {
	material "/maps/particles/world_space_ribbon.mat"
	init_emit_count 10
	emit_per_second 0
	max_ribbons 10
	max_ribbon_length 10
	init_ribbons_count 0

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_emission : float

	var t : float
	var r : float

	fn output() {
		let pos : float3 = mix(entity_position, g_target, r);

		i_position.z = pos.z + noise(t + 123.456) * JITTER_SIZE - JITTER_SIZE * 0.5;
		i_position.y = pos.y + noise(t) * JITTER_SIZE - JITTER_SIZE * 0.5;
		i_position.x = pos.x + noise(t + 456.789) * JITTER_SIZE - JITTER_SIZE * 0.5;
		i_scale = 0.02;//(1 - pos.y * 0.05) * 0.1;
		i_color = {0.3, 0.3, 1, 1};
		i_emission = 5;
	}

	fn emit() {
		r = emit_index / 10.0;
		t = random(0, 9000);
	}

	fn update() {
		t = t + time_delta * JITTER_SPEED;
	}
}

emitter spark_up {
	material "/maps/particles/world_space_particle.mat"
	init_emit_count 0
	emit_per_second 10
	
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
		vel = sphere(0.5);
		vel.y = 1;
		t = 0;
	}

	fn output() {
		i_position = pos;
		i_scale = 0.03;
		i_color = {1, 0.3, 0.2, 1} * (1 - t);
		i_rot = 0;
		i_frame = 0;
		i_emission = 10 * (1 - t);
	}
}

emitter sparks {
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
		vel.y = vel.y - time_delta * 9.8;
		if t > 1 {
			kill();
		}
	}
	fn emit() {
		pos = entity_position;
		vel = sphere(1);
		t = 0;
	}

	fn output() {
		i_position = pos;
		i_scale = 0.03;
		i_color = {1, 0.3, 0.2, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 20 * (1 - t) * (1 - t);
	}
}
