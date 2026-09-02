#pragma once

#include "engine/plugin.h"
#include "engine/resource.h"
#include "core/path.h"
#include "core/span.h"
#include "../../external/evox/capi.h"

namespace Lumix {

struct NativeFunctionKey {
	StringView unit_path;
	StringView name;

	bool operator==(const NativeFunctionKey& rhs) const { return unit_path == rhs.unit_path && name == rhs.name; }
};

struct NativeFunctionKeyHash {
	static u32 get(const NativeFunctionKey& key) {
		const u32 unit_hash = RuntimeHash32(key.unit_path.data, key.unit_path.size()).getHashValue();
		const u32 name_hash = RuntimeHash32(key.name.data, key.name.size()).getHashValue();
		return unit_hash ^ (name_hash + 0x9e3779b9 + (unit_hash << 6) + (unit_hash >> 2));
	}
};

struct EvoxModule;

struct EvoxSystem : ISystem {
	virtual Engine& getEngine() = 0;
	virtual void registerModule(EvoxModule& module) = 0;
	virtual void unregisterModule(EvoxModule& module) = 0;
	virtual bool isReady() const = 0;
	virtual Span<const ex_type*> getEvoxDataTypes() const = 0;
	virtual ex_runtime* getDebugRuntime() = 0;
	virtual const Path& getDebugPath() const = 0;
	virtual bool setDebugBreakpoint(const Path& source, u32 line) = 0;
	virtual bool removeDebugBreakpoint(const Path& source, u32 line) = 0;
};

//@ module EvoxModule evox "Evox"
struct EvoxModule : IModule {
	virtual void createEvox(EntityRef entity) = 0;
	virtual void destroyEvox(EntityRef entity) = 0;

	//@ component Evox id evox label "Data"
	//@ end
	
	virtual Span<const u8> getEvoxData(const char* type_name) = 0;
	virtual void setEvoxDataTypes(Span<const ex_type*> types) = 0;
	virtual void clearEvoxData() = 0;
	virtual bool isReady() const = 0;
	virtual Span<const ex_type*> getEvoxDataTypes() const = 0;
	virtual u32 getEvoxDataCount(EntityRef entity) const = 0;
	virtual const ex_type* getEvoxDataType(EntityRef entity, u32 index) const = 0;
	virtual const void* getEvoxData(EntityRef entity, const ex_type* type) const = 0;
	virtual bool addEvoxData(EntityRef entity, const ex_type* type) = 0;
	virtual bool removeEvoxData(EntityRef entity, const ex_type* type) = 0;
	virtual bool hasEvoxData(EntityRef entity, const ex_type* type) const = 0;
	virtual ex_runtime* getDebugRuntime() = 0;
	virtual const Path& getDebugPath() const = 0;
	virtual bool setDebugBreakpoint(const Path& source, u32 line) = 0;
	virtual bool removeDebugBreakpoint(const Path& source, u32 line) = 0;
};

} // namespace Lumix
