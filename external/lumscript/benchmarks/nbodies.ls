// N-bodies benchmark
// Port of https://github.com/Beariish/bolt/blob/main/benchmarks/nbodies.bolt
//
// Differences from the bolt original:
// - fixed-size arrays instead of dynamic arrays; pairs store body indices
// - pow(d, -1.5) is expressed as 1 / (d * sqrt(d)) since std:math has no pow
// - runs a single sim_system(500000) instead of the bench() harness

import "std:math" as math

const pi = 3.14159265358979323846;
const solar_mass = 4.0 * pi * pi;
const days_per_year = 365.24;

struct Body {
	px : f64;
	py : f64;
	pz : f64;
	vx : f64;
	vy : f64;
	vz : f64;
	mass : f64;
}

struct BodyPair {
	first : i32;
	second : i32;
}

fn combinations(n : i32, pairs : []BodyPair) : void {
	var count : i32 = 0;
	for i = 0..n - 1 {
		for j = i + 1..n {
			pairs[count] = BodyPair { i, j };
			count += 1;
		}
	}
}

fn make_system(bodies : []Body) : void {
	// Sun
	bodies[0] = Body {
		0.0, 0.0, 0.0,
		0.0, 0.0, 0.0,
		solar_mass
	};

	// Jupiter
	bodies[1] = Body {
		4.84143144246472090, -1.16032004402742839, -0.103622044471123109,
		0.00166007664274403694 * days_per_year, 0.00769901118419740425 * days_per_year, -0.0000690460016972063023 * days_per_year,
		0.000954791938424326609 * solar_mass
	};

	// Saturn
	bodies[2] = Body {
		8.34336671824457987, 4.12479856412430479, -0.403523417114321381,
		-0.00276742510726862411 * days_per_year, 0.00499852801234917238 * days_per_year, 0.0000230417297573763929 * days_per_year,
		0.000285885980666130812 * solar_mass
	};

	// Uranus
	bodies[3] = Body {
		12.8943695621391310, -15.1111514016986312, -0.223307578892655734,
		0.00296460137564761618 * days_per_year, 0.00237847173959480950 * days_per_year, -0.0000296589568540237556 * days_per_year,
		0.0000436624404335156298 * solar_mass
	};

	// Neptune
	bodies[4] = Body {
		15.3796971148509165, -25.9193146099879641, 0.179258772950371181,
		0.00268067772490389322 * days_per_year, 0.00162824170038242295 * days_per_year, -0.0000951592254519715870 * days_per_year,
		0.0000515138902046611451 * solar_mass
	};
}

fn offset_momentum(bodies : []Body) : void {
	var px = 0.0;
	var py = 0.0;
	var pz = 0.0;

	for bi = 0..length(bodies) {
		px -= bodies[bi].vx * bodies[bi].mass;
		py -= bodies[bi].vy * bodies[bi].mass;
		pz -= bodies[bi].vz * bodies[bi].mass;
	}

	bodies[0].vx = px / bodies[0].mass;
	bodies[0].vy = py / bodies[0].mass;
	bodies[0].vz = pz / bodies[0].mass;
}

fn sum_energy(bodies : []Body, pairs : []BodyPair) : f64 {
	var e = 0.0;

	for pi_ = 0..length(pairs) {
		const b1 = bodies[pairs[pi_].first];
		const b2 = bodies[pairs[pi_].second];

		const dx = b1.px - b2.px;
		const dy = b1.py - b2.py;
		const dz = b1.pz - b2.pz;

		e -= (b1.mass * b2.mass) / math.sqrt_f64(dx * dx + dy * dy + dz * dz);
	}

	for bi = 0..length(bodies) {
		const body = bodies[bi];
		e += body.mass * ((body.vx * body.vx + body.vy * body.vy + body.vz * body.vz) / 2.0);
	}

	return e;
}

fn advance(time : f64, iters : i32, bodies : []Body, pairs : []BodyPair) : void {
	const n_pairs = length(pairs);
	const n_bodies = length(bodies);

	for i = 0..iters {
		for pi_ = 0..n_pairs {
			const first = pairs[pi_].first;
			const second = pairs[pi_].second;

			const dx = bodies[first].px - bodies[second].px;
			const dy = bodies[first].py - bodies[second].py;
			const dz = bodies[first].pz - bodies[second].pz;

			const d = dx * dx + dy * dy + dz * dz;
			const mag = time / (d * math.sqrt_f64(d));

			const b1m = bodies[first].mass * mag;
			const b2m = bodies[second].mass * mag;

			bodies[first].vx -= dx * b2m;
			bodies[first].vy -= dy * b2m;
			bodies[first].vz -= dz * b2m;

			bodies[second].vx += dx * b1m;
			bodies[second].vy += dy * b1m;
			bodies[second].vz += dz * b1m;
		}

		for bi = 0..n_bodies {
			bodies[bi].px += time * bodies[bi].vx;
			bodies[bi].py += time * bodies[bi].vy;
			bodies[bi].pz += time * bodies[bi].vz;
		}
	}
}

fn sim_system(iters : i32) : f64 {
	var bodies : [5]Body = undefined;
	var pairs : [10]BodyPair = undefined;

	make_system(bodies);
	combinations(5, pairs);

	offset_momentum(bodies);
	sum_energy(bodies, pairs);
	advance(0.01, iters, bodies, pairs);
	return sum_energy(bodies, pairs);
}

fn main() : f64 {
	return sim_system(500000);
}
