-- Mandelbrot benchmark
-- Lua port of mandel.evox

local sqrt = math.sqrt

local function norm2(re, im)
	return re * re - im * (-im)
end

local function cabs(re, im)
	return sqrt(norm2(re, im))
end

local function level(x, y)
	local zre = x
	local zim = y

	for l = 0, 254 do
		local tre = zre * zre - zim * zim
		local tim = zre * zim + zim * zre

		zre = tre + x
		zim = tim + y

		if cabs(zre, zim) > 2.0 then
			return l
		end
	end

	return 255
end

local function run()
	local xmin = -2.0
	local ymin = -2.0
	local n = 256

	local dx = 4.0 / n
	local dy = 4.0 / n

	local result = 0
	for i = 0, n - 1 do
		local x = xmin + i * dx
		for j = 0, n - 1 do
			local y = ymin + j * dy
			result = result + level(x, y)
		end
	end

	return result
end

local start = os.clock()
local result = run()
local elapsed = (os.clock() - start) * 1000.0
io.stderr:write(string.format("Time: %.2f ms\n", elapsed))
print(result)
