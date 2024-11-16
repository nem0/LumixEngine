-- TODO proper error handling
local cores = function(co, ...)
	local res, err = coroutine.resume(co, ...)
	if not res and err then
		error(err .. "\nCallstack:\n" .. debug.traceback())
	end
	return res and err ~= false
end

local function iff(condition, a, b)
	if condition then
		return a
	else
		return b
	end
end

local function easeInOutCirc(x: number): number
	return iff(x < 0.5
	  , (1 - math.sqrt(1 - math.pow(2 * x, 2))) / 2
	  , (math.sqrt(1 - math.pow(-2 * x + 2, 2)) + 1) / 2)
end

return {

lerp = function(obj, property, start_val, end_val, length)
	local time = 0
	while time < length do
		local rel = time / length
		obj[property] = start_val + (end_val - start_val) * rel
		td = coroutine.yield()
		time = time + td
	end
	return false
end,

lerpVec3 = function(obj, property, start_val, end_val, length)
	local time = 0
	while time < length do
		local rel = time / length
		obj[property] = {
			start_val[1] + (end_val[1] - start_val[1]) * rel,
			start_val[2] + (end_val[2] - start_val[2]) * rel,
			start_val[3] + (end_val[3] - start_val[3]) * rel
		}
		td = coroutine.yield()
		time = time + td
	end
end,

lerpAnimatorFloat = function(obj, property, from, to, length)
	local time = 0
	while time < length do
		local rel = time / length
		local val = from + (to - from) * rel
		val = easeInOutCirc(val)
		obj.animator:setFloatInput(property, val)
		td = coroutine.yield()
		time += td
	end
end,

-- takes a list of functions, converts them to couroutines and runs them "in parallel"
-- it runs as long as there are coroutines still not finished
parallel = function(...)
	local to_tick = {}
	for _, v in ipairs({...}) do
		table.insert(to_tick, coroutine.create(function() 
			v()
			return false
		end))
	end

	while #to_tick > 0 do
		local n = #to_tick
		for i = n, 1, -1 do
			if not cores(to_tick[i], td) then
				table.remove(to_tick, i)
			end
		end
		td = coroutine.yield()
	end
end,

-- wait for specified time
wait = function(length)
	local time = 0
	while time < length do
		td = coroutine.yield()
		time = time + td
	end
end,

-- conver the function to coroutine and run it
run = function(fn)
	local co = coroutine.create(fn)
	table.insert(_G["global"].running_coroutines, co)
end,

-- tick all running coroutines
tick = function(td)
	local global = _G["global"]
	local n = #global.running_coroutines
	for i = n, 1, -1 do
		local co = global.running_coroutines[i]
		if not cores(co, td) then
			table.remove(global.running_coroutines, i)
		end
	end
end

}
