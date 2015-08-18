#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{
	class LUMIX_RENDERER_API MaterialManager : public ResourceManagerBase
	{
	public:
		MaterialManager(IAllocator& allocator) 
			: ResourceManagerBase(allocator)
			, m_allocator(allocator)
		{}
		~MaterialManager() {}

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
	};
}