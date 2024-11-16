point0 = point0 or {}
point1 = point1 or {}
point2 = point2 or {}
point3 = point3 or {}
Editor.setPropertyType(this, "point0", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point1", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point2", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "point3", Editor.ENTITY_PROPERTY)

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