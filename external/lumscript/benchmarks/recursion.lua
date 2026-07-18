-- Recursion: Fibonacci benchmark
-- Lua port of recursion.ls

local function fib(n)
	if n <= 1 then
		return n
	end
	return fib(n - 1) + fib(n - 2)
end

local start = os.clock()
local result = fib(30)
local elapsed = (os.clock() - start) * 1000.0
io.stderr:write(string.format("Time: %.2f ms\n", elapsed))
print(result)
