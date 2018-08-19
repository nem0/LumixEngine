#pragma once

#include "engine/matrix.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"

namespace Lumix
{


namespace FS
{
	class FileSystem;
	struct IFile;
}

namespace Reflection
{
	struct  PropertyBase;
}


class JsonSerializer;


class PropertyAnimationManager final : public ResourceManagerBase
{
public:
	explicit PropertyAnimationManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{}
	~PropertyAnimationManager() {}
	IAllocator& getAllocator() { return m_allocator; }

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


class PropertyAnimation final : public Resource
{
public:
	struct Curve
	{
		Curve(IAllocator& allocator) : frames(allocator), values(allocator) {}

		ComponentType cmp_type;
		const Reflection::PropertyBase* property;
		
		Array<int> frames;
		Array<float> values;
	};

	PropertyAnimation(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	Curve& addCurve();
	bool save(JsonSerializer& serializer);

	Array<Curve> curves;
	int fps;

	static const ResourceType TYPE;

private:
	IAllocator& getAllocator() const;

	void unload() override;
	bool load(FS::IFile& file) override;
};


} // namespace Lumix
