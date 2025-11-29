function goToRandomPoint()
	local speed = math.random(2, 5)
	local dst = {
		math.random(10, 60), 
		0, 
		math.random(10, 20)
	}
	this.navmesh_agent:navigate(dst, speed, 0.5)
	local speed_input = this.animator:getInputIndex("speed_y")
	this.animator:setFloatInput(speed_input, speed)
end

function start()
	goToRandomPoint()
end

function onPathFinished()
	goToRandomPoint()
end
