local points = {}

function update(time_delta)
	if #points < 500 then
		table.insert(points, this.position)
	end
	for i = 2, #points do
		this.world.renderer:addDebugLine(points[i - 1], points[i], {1, 0, 0})
	end
end
