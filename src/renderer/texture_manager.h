#pragma once

#include "engine/resource_manager.h"

namespace Lumix
{
	class LUMIX_RENDERER_API TextureManager final : public ResourceManager
	{
	public:
		explicit TextureManager(class Renderer& renderer, IAllocator& allocator);
		~TextureManager();

		u8* getBuffer(i32 size);

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		Renderer& m_renderer;
		u8* m_buffer;
		i32 m_buffer_size;
	};
}