#pragma once

#include "engine/resource_manager_base.h"

namespace Lumix
{

	class Renderer;

	class LUMIX_RENDERER_API ShaderBinaryManager LUMIX_FINAL : public ResourceManagerBase
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

	class LUMIX_RENDERER_API ShaderManager LUMIX_FINAL : public ResourceManagerBase
	{
	public:
		ShaderManager(Renderer& renderer, IAllocator& allocator);
		~ShaderManager();

		Renderer& getRenderer() { return m_renderer; }
		u8* getBuffer(i32 size);

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		u8* m_buffer;
		i32 m_buffer_size;
		Renderer& m_renderer;
	};
}