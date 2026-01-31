inherit "maps/demo/button"

cubeA = cubeA or black.h.Entity.NULL
cubeB = cubeB or black.h.Entity.NULL
sphereA = sphereA or black.h.Entity.NULL
sphereB = sphereB or black.h.Entity.NULL

local cubeA_pos = {}
local cubeB_pos = {}
local sphereA_pos = {}
local sphereB_pos = {}

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
