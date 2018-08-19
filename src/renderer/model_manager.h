#pragma once

#include "engine/resource_manager_base.h"

namespace Lumix
{

	class Renderer;


	class LUMIX_RENDERER_API ModelManager final : public ResourceManagerBase
	{
	public:
		ModelManager(Renderer& renderer, IAllocator& allocator)
			: ResourceManagerBase(allocator)
			, m_allocator(allocator)
			, m_renderer(renderer)
		{}

		~ModelManager() {}

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		Renderer& m_renderer;
	};
}