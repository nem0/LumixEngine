-- Fannkuch benchmark
-- Lua port of fannkuch.evox (tables are 1-based here, unlike the 0-based ports;
-- stored permutation values stay 0-based so the flip logic matches)

local function fannkuch(n, perm1, perm, count)
	for i = 1, n do
		perm1[i] = i - 1
	end

	local flips = 0
	local nperm = 0
	local checksum = 0
	local r = n

	while r > 0 do
		local i = r
		while i > 0 do
			count[i] = i
			i = i - 1
		end

		for j = 1, n do
			perm[j] = perm1[j]
		end

		local f = 0
		local k = perm[1]

		while k ~= 0 do
			local half = (k + 1) // 2
			for j = 0, half - 1 do
				local t = perm[j + 1]
				perm[j + 1] = perm[k - j + 1]
				perm[k - j + 1] = t
			end

			k = perm[1]
			f = f + 1
		end

		if f > flips then
			flips = f
		end
		if nperm % 2 == 0 then
			checksum = checksum + f
		else
			checksum = checksum - f
		end

		r = 1
		while true do
			if r == n then
				return flips
			end

			local p0 = perm1[1]
			for j = 1, r do
				perm1[j] = perm1[j + 1]
			end

			perm1[r + 1] = p0
			count[r + 1] = count[r + 1] - 1
			if count[r + 1] > 0 then
				break
			end

			r = r + 1
		end

		nperm = nperm + 1
	end

	return flips
end

local start = os.clock()
local result = fannkuch(9, {}, {}, {})
local elapsed = (os.clock() - start) * 1000.0
io.stderr:write(string.format("Time: %.2f ms\n", elapsed))
print(result)
