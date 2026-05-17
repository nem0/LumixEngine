#pragma once

#include "engine/plugin.h"
#include "engine/resource.h"
#include "core/path.h"
#include "lumscript/lumscript.h"

namespace Lumix {

struct LumScriptSystem : ISystem {
	virtual void updateScripts(float time_delta) = 0;
	virtual Engine& getEngine() = 0;
};

struct LumScriptModule : IModule {
	virtual void load(const Path& path) = 0;
	virtual bool isReady() const = 0;
};

} // namespace Lumix
