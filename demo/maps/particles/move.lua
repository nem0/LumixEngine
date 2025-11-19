local pos = {}
local t = 0

function start()
	pos = this.position
end

function update(time_delta)
	t = t + time_delta
	this.position = {
		pos[1] + math.cos(t) * 3,
		pos[2],
		pos[3] + math.sin(t) * 3
	}
end
