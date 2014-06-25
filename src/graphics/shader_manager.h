#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{
	class LUMIX_ENGINE_API ShaderManager : public ResourceManagerBase
	{
	public:
		ShaderManager();
		~ShaderManager();

		uint8_t* getBuffer(int32_t size);

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		uint8_t* m_buffer;
		int32_t m_buffer_size;
	};
}