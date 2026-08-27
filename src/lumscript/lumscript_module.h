#pragma once

#include "engine/plugin.h"
#include "engine/resource.h"
#include "core/path.h"
#include "core/span.h"
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

struct LumScriptModule;

struct LumScriptSystem : ISystem {
	virtual Engine& getEngine() = 0;
	virtual void registerModule(LumScriptModule& module) = 0;
	virtual void unregisterModule(LumScriptModule& module) = 0;
	virtual bool isReady() const = 0;
	virtual Span<const ls_type*> getLumScriptDataTypes() const = 0;
	virtual ls_runtime* getDebugRuntime() = 0;
	virtual const Path& getDebugPath() const = 0;
	virtual bool setDebugBreakpoint(const Path& source, u32 line) = 0;
	virtual bool removeDebugBreakpoint(const Path& source, u32 line) = 0;
};

//@ module LumScriptModule lumscript "LumScript"
struct LumScriptModule : IModule {
	virtual void createLumScript(EntityRef entity) = 0;
	virtual void destroyLumScript(EntityRef entity) = 0;

	//@ component LumScript id lumscript label "Data"
	//@ end

	virtual void setLumScriptDataTypes(Span<const ls_type*> types) = 0;
	virtual void clearLumScriptData() = 0;
	virtual bool isReady() const = 0;
	virtual Span<const ls_type*> getLumScriptDataTypes() const = 0;
	virtual u32 getLumScriptDataCount(EntityRef entity) const = 0;
	virtual const ls_type* getLumScriptDataType(EntityRef entity, u32 index) const = 0;
	virtual const void* getLumScriptData(EntityRef entity, const ls_type* type) const = 0;
	virtual bool addLumScriptData(EntityRef entity, const ls_type* type) = 0;
	virtual bool removeLumScriptData(EntityRef entity, const ls_type* type) = 0;
	virtual bool hasLumScriptData(EntityRef entity, const ls_type* type) const = 0;
	virtual ls_runtime* getDebugRuntime() = 0;
	virtual const Path& getDebugPath() const = 0;
	virtual bool setDebugBreakpoint(const Path& source, u32 line) = 0;
	virtual bool removeDebugBreakpoint(const Path& source, u32 line) = 0;
};

} // namespace Lumix
