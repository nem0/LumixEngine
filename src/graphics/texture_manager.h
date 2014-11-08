#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{
	class LUMIX_ENGINE_API TextureManager : public ResourceManagerBase
	{
	public:
		TextureManager(IAllocator& allocator);
		~TextureManager();

		uint8_t* getBuffer(int32_t size);

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		uint8_t* m_buffer;
		int32_t m_buffer_size;
	};
}