#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{
	class LUMIX_RENDERER_API TextureManager : public ResourceManagerBase
	{
	public:
		explicit TextureManager(IAllocator& allocator);
		~TextureManager();

		uint8* getBuffer(int32 size);

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		uint8* m_buffer;
		int32 m_buffer_size;
	};
}