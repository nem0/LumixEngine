#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{

	class Renderer;

	class LUMIX_RENDERER_API MaterialManager : public ResourceManagerBase
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
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		Renderer& m_renderer;
	};
}