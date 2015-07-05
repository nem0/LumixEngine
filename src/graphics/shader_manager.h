#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{

	class Renderer;

	class LUMIX_ENGINE_API ShaderManager : public ResourceManagerBase
	{
	public:
		ShaderManager(Renderer& renderer, IAllocator& allocator);
		~ShaderManager();

		Renderer& getRenderer() { return m_renderer; }
		uint8_t* getBuffer(int32_t size);

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		uint8_t* m_buffer;
		int32_t m_buffer_size;
		Renderer& m_renderer;
	};
}