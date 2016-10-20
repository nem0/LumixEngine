#pragma once

#include "engine/resource_manager_base.h"

namespace Lumix
{

	class Renderer;

	class LUMIX_RENDERER_API MaterialManager LUMIX_FINAL : public ResourceManagerBase
	{
	public:
		MaterialManager(Renderer& renderer, IAllocator& allocator)
			: ResourceManagerBase(allocator)
			, m_renderer(renderer)
			, m_allocator(allocator)
		{}
		~MaterialManager() {}

		Renderer& getRenderer() { return m_renderer; }

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		Renderer& m_renderer;
	};
}