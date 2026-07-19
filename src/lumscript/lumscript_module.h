#pragma once

#include "engine/plugin.h"
#include "engine/resource.h"
#include "core/path.h"
#include "../../external/lumscript/capi.h"

namespace Lumix {

struct NativeFunctionKey {
	StringView unit_path;
	StringView name;

	bool operator==(const NativeFunctionKey& rhs) const { return unit_path == rhs.unit_path && name == rhs.name; }
};

struct NativeFunctionKeyHash {
	static u32 get(const NativeFunctionKey& key) {
		const u32 unit_hash = RuntimeHash32(key.unit_path.begin, key.unit_path.size()).getHashValue();
		const u32 name_hash = RuntimeHash32(key.name.begin, key.name.size()).getHashValue();
		return unit_hash ^ (name_hash + 0x9e3779b9 + (unit_hash << 6) + (unit_hash >> 2));
	}
};

struct LumScriptSystem : ISystem {
	virtual void updateScripts(float time_delta) = 0;
	virtual Engine& getEngine() = 0;
};

struct LumScriptModule : IModule {
	virtual void load(const Path& path) = 0;
	virtual bool isReady() const = 0;
	virtual ls_runtime* getDebugRuntime() = 0;
	virtual const Path& getDebugPath() const = 0;
	virtual void setDebugEnabled(bool enabled) = 0;
	virtual bool setDebugBreakpoint(const Path& source, u32 line) = 0;
	virtual bool removeDebugBreakpoint(const Path& source, u32 line) = 0;
};

} // namespace Lumix
