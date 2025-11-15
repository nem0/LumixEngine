const hspread = 10.5;

emitter explosion {
	material "/maps/fireworks/explosion.mat"
	init_emit_count 100

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var vel : float3
	var t : float
	var pos : float3
	var col : float3
	
	in in_pos : float3
	in in_col : float3

	fn output {
		i_position = pos;
		i_scale = (1 - t) * 0.2;
		i_color.r = col.r;
		i_color.g = col.g;
		i_color.b = col.b;
		i_color.a = 1;
		i_rot = 0;
		i_frame = 0;
		i_emission = 100 * t;
	}

	fn emit {
		t = 0;
		pos = in_pos;
		col = in_col;
		vel.x = random(-hspread, hspread);
		vel.y = random(-hspread, hspread);
		vel.z = random(-hspread, hspread);
	}

	fn update {
		t = t + time_delta;
		vel.y = vel.y - time_delta * 0.3;
		pos = pos + vel * time_delta;
		kill(t > 1);
	}
}

emitter fireworks {
	material "/maps/fireworks/explosion.mat"
	emit_per_second 5

	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var vel : float3
	var t : float
	
	fn output {
		i_position = vel * t;
		i_scale = 0.1;
		i_color = {1, 0, 1, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 10;
	}

	fn emit {
		t = 0;
		vel.x = random(-hspread, hspread);
		vel.y = 30;
		vel.z = random(-hspread, hspread);
	}

	fn update {
		t = t + time_delta;
		vel.y = vel.y - time_delta * 9.8;
		emit(explosion, t > 1.5) {
			in_pos = vel * t;
			in_col.x = random(0, 1);
			in_col.y = random(0, 1);
			in_col.z = random(0, 1);
		};
		kill(t > 1.5);
	}
}














