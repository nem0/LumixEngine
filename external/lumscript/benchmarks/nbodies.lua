-- N-bodies benchmark
-- Lua port of nbodies.ls (tables are 1-based here, unlike the 0-based ports)

local sqrt = math.sqrt

local pi = 3.14159265358979323846
local solar_mass = 4.0 * pi * pi
local days_per_year = 365.24

local function combinations(n, pairs_)
	local count = 1
	for i = 1, n - 1 do
		for j = i + 1, n do
			pairs_[count] = { first = i, second = j }
			count = count + 1
		end
	end
end

local function make_system(bodies)
	-- Sun
	bodies[1] = {
		px = 0.0, py = 0.0, pz = 0.0,
		vx = 0.0, vy = 0.0, vz = 0.0,
		mass = solar_mass
	}

	-- Jupiter
	bodies[2] = {
		px = 4.84143144246472090, py = -1.16032004402742839, pz = -0.103622044471123109,
		vx = 0.00166007664274403694 * days_per_year, vy = 0.00769901118419740425 * days_per_year, vz = -0.0000690460016972063023 * days_per_year,
		mass = 0.000954791938424326609 * solar_mass
	}

	-- Saturn
	bodies[3] = {
		px = 8.34336671824457987, py = 4.12479856412430479, pz = -0.403523417114321381,
		vx = -0.00276742510726862411 * days_per_year, vy = 0.00499852801234917238 * days_per_year, vz = 0.0000230417297573763929 * days_per_year,
		mass = 0.000285885980666130812 * solar_mass
	}

	-- Uranus
	bodies[4] = {
		px = 12.8943695621391310, py = -15.1111514016986312, pz = -0.223307578892655734,
		vx = 0.00296460137564761618 * days_per_year, vy = 0.00237847173959480950 * days_per_year, vz = -0.0000296589568540237556 * days_per_year,
		mass = 0.0000436624404335156298 * solar_mass
	}

	-- Neptune
	bodies[5] = {
		px = 15.3796971148509165, py = -25.9193146099879641, pz = 0.179258772950371181,
		vx = 0.00268067772490389322 * days_per_year, vy = 0.00162824170038242295 * days_per_year, vz = -0.0000951592254519715870 * days_per_year,
		mass = 0.0000515138902046611451 * solar_mass
	}
end

local function offset_momentum(bodies)
	local px = 0.0
	local py = 0.0
	local pz = 0.0

	for bi = 1, #bodies do
		px = px - bodies[bi].vx * bodies[bi].mass
		py = py - bodies[bi].vy * bodies[bi].mass
		pz = pz - bodies[bi].vz * bodies[bi].mass
	end

	bodies[1].vx = px / bodies[1].mass
	bodies[1].vy = py / bodies[1].mass
	bodies[1].vz = pz / bodies[1].mass
end

local function sum_energy(bodies, pairs_)
	local e = 0.0

	for pi_ = 1, #pairs_ do
		local b1 = bodies[pairs_[pi_].first]
		local b2 = bodies[pairs_[pi_].second]

		local dx = b1.px - b2.px
		local dy = b1.py - b2.py
		local dz = b1.pz - b2.pz

		e = e - (b1.mass * b2.mass) / sqrt(dx * dx + dy * dy + dz * dz)
	end

	for bi = 1, #bodies do
		local body = bodies[bi]
		e = e + body.mass * ((body.vx * body.vx + body.vy * body.vy + body.vz * body.vz) / 2.0)
	end

	return e
end

local function advance(time, iters, bodies, pairs_)
	local n_pairs = #pairs_
	local n_bodies = #bodies

	for i = 1, iters do
		for pi_ = 1, n_pairs do
			local first = pairs_[pi_].first
			local second = pairs_[pi_].second

			local dx = bodies[first].px - bodies[second].px
			local dy = bodies[first].py - bodies[second].py
			local dz = bodies[first].pz - bodies[second].pz

			local d = dx * dx + dy * dy + dz * dz
			local mag = time / (d * sqrt(d))

			local b1m = bodies[first].mass * mag
			local b2m = bodies[second].mass * mag

			bodies[first].vx = bodies[first].vx - dx * b2m
			bodies[first].vy = bodies[first].vy - dy * b2m
			bodies[first].vz = bodies[first].vz - dz * b2m

			bodies[second].vx = bodies[second].vx + dx * b1m
			bodies[second].vy = bodies[second].vy + dy * b1m
			bodies[second].vz = bodies[second].vz + dz * b1m
		end

		for bi = 1, n_bodies do
			bodies[bi].px = bodies[bi].px + time * bodies[bi].vx
			bodies[bi].py = bodies[bi].py + time * bodies[bi].vy
			bodies[bi].pz = bodies[bi].pz + time * bodies[bi].vz
		end
	end
end

local function sim_system(iters)
	local bodies = {}
	local pairs_ = {}

	make_system(bodies)
	combinations(5, pairs_)

	offset_momentum(bodies)
	sum_energy(bodies, pairs_)
	advance(0.01, iters, bodies, pairs_)
	return sum_energy(bodies, pairs_)
end

local start = os.clock()
local result = sim_system(500000)
local elapsed = (os.clock() - start) * 1000.0
io.stderr:write(string.format("Time: %.2f ms\n", elapsed))
print(string.format("%.6f", result))
