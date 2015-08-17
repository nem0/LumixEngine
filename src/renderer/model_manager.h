#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{

	class Renderer;


	class LUMIX_RENDERER_API ModelManager : public ResourceManagerBase
	{
	public:
		ModelManager(IAllocator& allocator, Renderer& renderer)
			: ResourceManagerBase(allocator)
			, m_allocator(allocator)
			, m_renderer(renderer)
		{}

		~ModelManager() {}

		Renderer& getRenderer() { return m_renderer; }

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		Renderer& m_renderer;
	};
}