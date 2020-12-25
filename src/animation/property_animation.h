#pragma once

#include "engine/resource.h"

struct lua_State;

namespace Lumix {

namespace reflection { template <typename T> struct Property; }

struct PropertyAnimation final : Resource {
	struct Curve {
		Curve(IAllocator& allocator) : frames(allocator), values(allocator) {}

		ComponentType cmp_type;
		const reflection::Property<float>* property;
		
		Array<i32> frames;
		Array<float> values;
	};

	PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	Curve& addCurve();
	bool save(OutputMemoryStream& blob);

	IAllocator& m_allocator;
	Array<Curve> curves;
	int fps;

	static const ResourceType TYPE;

private:
	void LUA_curve(lua_State* L);
	void unload() override;
	bool load(u64 size, const u8* mem) override;
};


} // namespace Lumix
