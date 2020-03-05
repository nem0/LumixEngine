#pragma once

#include "engine/resource.h"


namespace Lumix
{


struct TextSerializer;
namespace Reflection { template <typename T> struct Property; }


struct PropertyAnimation final : Resource
{
public:
	struct Curve
	{
		Curve(IAllocator& allocator) : frames(allocator), values(allocator) {}

		ComponentType cmp_type;
		const Reflection::Property<float>* property;
		
		Array<int> frames;
		Array<float> values;
	};

	PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	Curve& addCurve();
	bool save(TextSerializer& serializer);

	IAllocator& m_allocator;
	Array<Curve> curves;
	int fps;

	static const ResourceType TYPE;

private:

	void unload() override;
	bool load(u64 size, const u8* mem) override;
};


} // namespace Lumix
