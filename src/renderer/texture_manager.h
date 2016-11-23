#pragma once

#include "engine/resource_manager_base.h"

namespace Lumix
{
	class LUMIX_RENDERER_API TextureManager LUMIX_FINAL : public ResourceManagerBase
	{
	public:
		explicit TextureManager(IAllocator& allocator);
		~TextureManager();

		u8* getBuffer(i32 size);

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		u8* m_buffer;
		i32 m_buffer_size;
	};
}