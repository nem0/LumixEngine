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


class PropertyAnimationManager LUMIX_FINAL : public ResourceManagerBase
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


class PropertyAnimation LUMIX_FINAL : public Resource
{
	public:
		PropertyAnimation(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);

		ResourceType getType() const override { return ResourceType("animation"); }

	private:
		IAllocator& getAllocator() const;

		void unload() override;
		bool load(FS::IFile& file) override;

	private:
		struct Curve
		{
			ComponentType cmp_type;
			const Reflection::PropertyBase* property;
		};

		struct Key
		{
			int frame;
			int curve;
			float value;
		};

		Array<Key> m_keys;
		Array<Curve> m_curves;
		int m_fps;
};


} // namespace Lumix
