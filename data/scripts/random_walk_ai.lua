point0 = {}
point1 = {}
point2 = {}
point3 = {}
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