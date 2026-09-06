// N-bodies benchmark
// Node.js port of nbodies.evox

const pi = 3.14159265358979323846;
const solar_mass = 4.0 * pi * pi;
const days_per_year = 365.24;

function combinations(n, pairs) {
	let count = 0;
	for (let i = 0; i < n - 1; i++) {
		for (let j = i + 1; j < n; j++) {
			pairs[count] = { first: i, second: j };
			count += 1;
		}
	}
}

function make_system(bodies) {
	// Sun
	bodies[0] = {
		px: 0.0, py: 0.0, pz: 0.0,
		vx: 0.0, vy: 0.0, vz: 0.0,
		mass: solar_mass
	};

	// Jupiter
	bodies[1] = {
		px: 4.84143144246472090, py: -1.16032004402742839, pz: -0.103622044471123109,
		vx: 0.00166007664274403694 * days_per_year, vy: 0.00769901118419740425 * days_per_year, vz: -0.0000690460016972063023 * days_per_year,
		mass: 0.000954791938424326609 * solar_mass
	};

	// Saturn
	bodies[2] = {
		px: 8.34336671824457987, py: 4.12479856412430479, pz: -0.403523417114321381,
		vx: -0.00276742510726862411 * days_per_year, vy: 0.00499852801234917238 * days_per_year, vz: 0.0000230417297573763929 * days_per_year,
		mass: 0.000285885980666130812 * solar_mass
	};

	// Uranus
	bodies[3] = {
		px: 12.8943695621391310, py: -15.1111514016986312, pz: -0.223307578892655734,
		vx: 0.00296460137564761618 * days_per_year, vy: 0.00237847173959480950 * days_per_year, vz: -0.0000296589568540237556 * days_per_year,
		mass: 0.0000436624404335156298 * solar_mass
	};

	// Neptune
	bodies[4] = {
		px: 15.3796971148509165, py: -25.9193146099879641, pz: 0.179258772950371181,
		vx: 0.00268067772490389322 * days_per_year, vy: 0.00162824170038242295 * days_per_year, vz: -0.0000951592254519715870 * days_per_year,
		mass: 0.0000515138902046611451 * solar_mass
	};
}

function offset_momentum(bodies) {
	let px = 0.0;
	let py = 0.0;
	let pz = 0.0;

	for (let bi = 0; bi < bodies.length; bi++) {
		px -= bodies[bi].vx * bodies[bi].mass;
		py -= bodies[bi].vy * bodies[bi].mass;
		pz -= bodies[bi].vz * bodies[bi].mass;
	}

	bodies[0].vx = px / bodies[0].mass;
	bodies[0].vy = py / bodies[0].mass;
	bodies[0].vz = pz / bodies[0].mass;
}

function sum_energy(bodies, pairs) {
	let e = 0.0;

	for (let pi_ = 0; pi_ < pairs.length; pi_++) {
		const b1 = bodies[pairs[pi_].first];
		const b2 = bodies[pairs[pi_].second];

		const dx = b1.px - b2.px;
		const dy = b1.py - b2.py;
		const dz = b1.pz - b2.pz;

		e -= (b1.mass * b2.mass) / Math.sqrt(dx * dx + dy * dy + dz * dz);
	}

	for (let bi = 0; bi < bodies.length; bi++) {
		const body = bodies[bi];
		e += body.mass * ((body.vx * body.vx + body.vy * body.vy + body.vz * body.vz) / 2.0);
	}

	return e;
}

function advance(time, iters, bodies, pairs) {
	const n_pairs = pairs.length;
	const n_bodies = bodies.length;

	for (let i = 0; i < iters; i++) {
		for (let pi_ = 0; pi_ < n_pairs; pi_++) {
			const first = pairs[pi_].first;
			const second = pairs[pi_].second;

			const dx = bodies[first].px - bodies[second].px;
			const dy = bodies[first].py - bodies[second].py;
			const dz = bodies[first].pz - bodies[second].pz;

			const d = dx * dx + dy * dy + dz * dz;
			const mag = time / (d * Math.sqrt(d));

			const b1m = bodies[first].mass * mag;
			const b2m = bodies[second].mass * mag;

			bodies[first].vx -= dx * b2m;
			bodies[first].vy -= dy * b2m;
			bodies[first].vz -= dz * b2m;

			bodies[second].vx += dx * b1m;
			bodies[second].vy += dy * b1m;
			bodies[second].vz += dz * b1m;
		}

		for (let bi = 0; bi < n_bodies; bi++) {
			bodies[bi].px += time * bodies[bi].vx;
			bodies[bi].py += time * bodies[bi].vy;
			bodies[bi].pz += time * bodies[bi].vz;
		}
	}
}

function sim_system(iters) {
	const bodies = new Array(5);
	const pairs = new Array(10);

	make_system(bodies);
	combinations(5, pairs);

	offset_momentum(bodies);
	sum_energy(bodies, pairs);
	advance(0.01, iters, bodies, pairs);
	return sum_energy(bodies, pairs);
}

const start = performance.now();
const result = sim_system(500000);
const elapsed = performance.now() - start;
console.error(`Time: ${elapsed.toFixed(2)} ms`);
console.log(result.toFixed(6));
