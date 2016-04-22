#pragma once

#include "engine/core/resource_manager_base.h"

namespace Lumix
{

	class Renderer;

	class LUMIX_RENDERER_API ShaderBinaryManager : public ResourceManagerBase
	{
	public:
		ShaderBinaryManager(Renderer& renderer, IAllocator& allocator);
		~ShaderBinaryManager();

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
	};

	class LUMIX_RENDERER_API ShaderManager : public ResourceManagerBase
	{
	public:
		ShaderManager(Renderer& renderer, IAllocator& allocator);
		~ShaderManager();

		Renderer& getRenderer() { return m_renderer; }
		uint8* getBuffer(int32 size);

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		uint8* m_buffer;
		int32 m_buffer_size;
		Renderer& m_renderer;
	};
}