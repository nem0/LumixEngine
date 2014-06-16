#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{
	class LUX_ENGINE_API TextureManager : public ResourceManagerBase
	{
	public:
		TextureManager();
		~TextureManager();

		uint8_t* getBuffer(int32_t size);

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		uint8_t* m_buffer;
		int32_t m_buffer_size;
	};
}