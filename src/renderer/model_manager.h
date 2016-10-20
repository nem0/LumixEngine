#pragma once

#include "engine/resource_manager_base.h"

namespace Lumix
{

	class Renderer;


	class LUMIX_RENDERER_API ModelManager LUMIX_FINAL : public ResourceManagerBase
	{
	public:
		ModelManager(IAllocator& allocator)
			: ResourceManagerBase(allocator)
			, m_allocator(allocator)
		{}

		~ModelManager() {}

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
	};
}