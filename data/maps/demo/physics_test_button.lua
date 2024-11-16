inherit "maps/demo/button"

cubeA = cubeA or {}
cubeB = cubeB or {}
sphereA = sphereA or {}
sphereB = sphereB or {}

local cubeA_pos = {}
local cubeB_pos = {}
local sphereA_pos = {}
local sphereB_pos = {}

Editor.setPropertyType(this, "cubeA", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "cubeB", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sphereA", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sphereB", Editor.ENTITY_PROPERTY)

function buttonPressed()
	-- reset objects' positions
	cubeA.position = cubeA_pos
	cubeB.position = cubeB_pos
	sphereA.position = sphereA_pos
	sphereB.position = sphereB_pos
end

function start()
	-- remember initial positions so we can reset them when button is pressed
	cubeA_pos = cubeA.position
	cubeB_pos = cubeB.position
	sphereA_pos = sphereA.position
	sphereB_pos = sphereB.position
end
