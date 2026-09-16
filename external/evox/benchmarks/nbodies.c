/* N-bodies benchmark
 * C port of nbodies.evox
 */

#include <math.h>
#include <stdio.h>
#include "../platform.h"

static const double pi = 3.14159265358979323846;
static const double days_per_year = 365.24;
#define SOLAR_MASS (4.0 * pi * pi)

typedef struct Body {
	double px, py, pz;
	double vx, vy, vz;
	double mass;
} Body;

typedef struct BodyPair {
	int first;
	int second;
} BodyPair;

static void combinations(int n, BodyPair* pairs) {
	int count = 0;
	for (int i = 0; i < n - 1; i++) {
		for (int j = i + 1; j < n; j++) {
			pairs[count].first = i;
			pairs[count].second = j;
			count += 1;
		}
	}
}

static void make_system(Body* bodies) {
	/* Sun */
	bodies[0] = (Body){
		0.0, 0.0, 0.0,
		0.0, 0.0, 0.0,
		SOLAR_MASS
	};

	/* Jupiter */
	bodies[1] = (Body){
		4.84143144246472090, -1.16032004402742839, -0.103622044471123109,
		0.00166007664274403694 * days_per_year, 0.00769901118419740425 * days_per_year, -0.0000690460016972063023 * days_per_year,
		0.000954791938424326609 * SOLAR_MASS
	};

	/* Saturn */
	bodies[2] = (Body){
		8.34336671824457987, 4.12479856412430479, -0.403523417114321381,
		-0.00276742510726862411 * days_per_year, 0.00499852801234917238 * days_per_year, 0.0000230417297573763929 * days_per_year,
		0.000285885980666130812 * SOLAR_MASS
	};

	/* Uranus */
	bodies[3] = (Body){
		12.8943695621391310, -15.1111514016986312, -0.223307578892655734,
		0.00296460137564761618 * days_per_year, 0.00237847173959480950 * days_per_year, -0.0000296589568540237556 * days_per_year,
		0.0000436624404335156298 * SOLAR_MASS
	};

	/* Neptune */
	bodies[4] = (Body){
		15.3796971148509165, -25.9193146099879641, 0.179258772950371181,
		0.00268067772490389322 * days_per_year, 0.00162824170038242295 * days_per_year, -0.0000951592254519715870 * days_per_year,
		0.0000515138902046611451 * SOLAR_MASS
	};
}

static void offset_momentum(Body* bodies, int n_bodies) {
	double px = 0.0;
	double py = 0.0;
	double pz = 0.0;

	for (int bi = 0; bi < n_bodies; bi++) {
		px -= bodies[bi].vx * bodies[bi].mass;
		py -= bodies[bi].vy * bodies[bi].mass;
		pz -= bodies[bi].vz * bodies[bi].mass;
	}

	bodies[0].vx = px / bodies[0].mass;
	bodies[0].vy = py / bodies[0].mass;
	bodies[0].vz = pz / bodies[0].mass;
}

static double sum_energy(const Body* bodies, int n_bodies, const BodyPair* pairs, int n_pairs) {
	double e = 0.0;

	for (int pi_ = 0; pi_ < n_pairs; pi_++) {
		const Body* b1 = &bodies[pairs[pi_].first];
		const Body* b2 = &bodies[pairs[pi_].second];

		const double dx = b1->px - b2->px;
		const double dy = b1->py - b2->py;
		const double dz = b1->pz - b2->pz;

		e -= (b1->mass * b2->mass) / sqrt(dx * dx + dy * dy + dz * dz);
	}

	for (int bi = 0; bi < n_bodies; bi++) {
		const Body* body = &bodies[bi];
		e += body->mass * ((body->vx * body->vx + body->vy * body->vy + body->vz * body->vz) / 2.0);
	}

	return e;
}

static void advance(double time, int iters, Body* bodies, int n_bodies, const BodyPair* pairs, int n_pairs) {
	for (int i = 0; i < iters; i++) {
		for (int pi_ = 0; pi_ < n_pairs; pi_++) {
			const int first = pairs[pi_].first;
			const int second = pairs[pi_].second;

			const double dx = bodies[first].px - bodies[second].px;
			const double dy = bodies[first].py - bodies[second].py;
			const double dz = bodies[first].pz - bodies[second].pz;

			const double d = dx * dx + dy * dy + dz * dz;
			const double mag = time / (d * sqrt(d));

			const double b1m = bodies[first].mass * mag;
			const double b2m = bodies[second].mass * mag;

			bodies[first].vx -= dx * b2m;
			bodies[first].vy -= dy * b2m;
			bodies[first].vz -= dz * b2m;

			bodies[second].vx += dx * b1m;
			bodies[second].vy += dy * b1m;
			bodies[second].vz += dz * b1m;
		}

		for (int bi = 0; bi < n_bodies; bi++) {
			bodies[bi].px += time * bodies[bi].vx;
			bodies[bi].py += time * bodies[bi].vy;
			bodies[bi].pz += time * bodies[bi].vz;
		}
	}
}

static double sim_system(int iters) {
	Body bodies[5];
	BodyPair pairs[10];

	make_system(bodies);
	combinations(5, pairs);

	offset_momentum(bodies, 5);
	sum_energy(bodies, 5, pairs, 10);
	advance(0.01, iters, bodies, 5, pairs, 10);
	return sum_energy(bodies, 5, pairs, 10);
}

int main(void) {
	const double start = ex_platform_now_ms();
	const double result = sim_system(500000);
	const double elapsed = ex_platform_now_ms() - start;
	fprintf(stderr, "Time: %.2f ms\n", elapsed);
	printf("%.6f\n", result);
	return 0;
}
