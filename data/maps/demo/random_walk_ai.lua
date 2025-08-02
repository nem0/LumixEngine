point0 = point0 or Lumix.Entity.NULL
point1 = point1 or Lumix.Entity.NULL
point2 = point2 or Lumix.Entity.NULL
point3 = point3 or Lumix.Entity.NULL

function goToRandomPoint()
	local pidx = math.random(4) - 1
	local p = point0.position
	if pidx == 1 then p = point1.position end
	if pidx == 2 then p = point2.position end
	if pidx == 3 then p = point3.position end

	this.navmesh_agent:navigate(p, 10, 1)
end

function start()
	goToRandomPoint()
end

function onPathFinished()
	goToRandomPoint()
end