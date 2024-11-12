local co = require "scripts/coroutine"

if _G["global"] ~= nil then
	LumixAPI.logError("Only one component with global.lua should exist in the scene")
end

local global = {
	running_coroutines = {}
}

_G["global"] = global

function onDestroy()
	_G["global"] = nil
end

function update(td)
	co.tick(td)
end