#pragma once

#include "engine/resource_manager.h"

namespace Lumix
{

	class Renderer;

	class LUMIX_RENDERER_API ShaderManager final : public ResourceManager
	{
	public:
		ShaderManager(Renderer& renderer, IAllocator& allocator);
		~ShaderManager();

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		Renderer& m_renderer;
	};
}