#pragma once

#include "gpu/gpu.hpp"
#ifndef _WIN32
	#include "engine/allocator.hpp"
	#include <string.h>
#endif

namespace Lumix {

struct DrawStream {
	DrawStream(struct Renderer& renderer);
	DrawStream(DrawStream&& rhs);
	~DrawStream();

	void createProgram(gpu::ProgramHandle prog, gpu::StateFlags state, const gpu::VertexDecl& decl, const char** srcs, const gpu::ShaderType* types, u32 num, const char** prefixes, u32 prefixes_count, const char* name);
	void createBuffer(gpu::BufferHandle buffer, gpu::BufferFlags flags, size_t size, const void* data);
	void createTexture(gpu::TextureHandle handle, u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const char* debug_name);
	void createTextureView(gpu::TextureHandle view, gpu::TextureHandle texture, u32 layer);
	void createBindGroup(gpu::BindGroupHandle group, Span<const gpu::BindGroupEntryDesc> descriptors);

	void destroy(gpu::BindGroupHandle group);
	void destroy(gpu::TextureHandle texture);
	void destroy(gpu::BufferHandle buffer);
	void destroy(gpu::ProgramHandle program);
	
	void setCurrentWindow(void* window_handle);
	void setFramebuffer(const gpu::TextureHandle* attachments, u32 num, gpu::TextureHandle ds, gpu::FramebufferFlags flags);
	void setFramebufferCube(gpu::TextureHandle cube, u32 face, u32 mip);
	void viewport(u32 x, u32 y, u32 w, u32 h);
	void scissor(u32 x,u32 y,u32 w,u32 h);
	void clear(gpu::ClearFlags flags, const float* color, float depth);
	
	void startCapture();
	void stopCapture();
	void pushDebugGroup(const char* msg);
	void popDebugGroup();
	void beginProfileBlock(const char* name, i64 link);
	void endProfileBlock();

	void useProgram(gpu::ProgramHandle program);
	
	void bind(u32 idx, gpu::BindGroupHandle group);
	void bindIndexBuffer(gpu::BufferHandle buffer);
	void bindVertexBuffer(u32 binding_idx, gpu::BufferHandle buffer, u32 buffer_offset, u32 stride);
	void bindTextures(const gpu::TextureHandle* handles, u32 offset, u32 count);
	void bindUniformBuffer(u32 ub_index, gpu::BufferHandle buffer, u32 offset, u32 size);
	void bindIndirectBuffer(gpu::BufferHandle buffer);
	void bindShaderBuffer(gpu::BufferHandle buffer, u32 binding_idx, gpu::BindShaderBufferFlags flags);
	void bindImageTexture(gpu::TextureHandle texture, u32 unit);

	void drawArrays(u32 offset, u32 count);
	void drawIndirect(gpu::DataType index_type, u32 indirect_buffer_offset);
	void drawIndexed(u32 offset, u32 count, gpu::DataType type);
	void drawArraysInstanced(u32 indices_count, u32 instances_count);
	void drawIndexedInstanced(u32 indices_count, u32 instances_count, gpu::DataType index_type);
	void dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z);
	
	void memoryBarrier(gpu::MemoryBarrierType type, gpu::BufferHandle);
	
	void copy(gpu::TextureHandle dst, gpu::TextureHandle src, u32 dst_x, u32 dst_y);
	void copy(gpu::BufferHandle dst, gpu::BufferHandle src, u32 dst_offset, u32 src_offset, u32 size);
	
	void readTexture(gpu::TextureHandle texture, u32 mip, Span<u8> buf);
	void generateMipmaps(gpu::TextureHandle texture);
	void setDebugName(gpu::TextureHandle texture, const char* debug_name);
	
	void update(gpu::TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, gpu::TextureFormat format, const void* buf, u32 size);
	void update(gpu::BufferHandle buffer, const void* data, size_t size);
	DrawStream& createSubstream();

	u8* userAlloc(u32 size);
	void freeMemory(void* data, IAllocator& allocator);
	void freeAlignedMemory(void* data, IAllocator& allocator);
	u8* pushFunction(void (*func)(void*), u32 payload_size);
	template <typename F> void pushLambda(const F& f);

	void run();
	void reset();
	void merge(DrawStream& rhs);

	struct Page;
private:
	enum class Instruction : u8;

	DrawStream(const DrawStream& rhs) = delete;
	void operator =(const DrawStream&) = delete;
	void operator =(DrawStream&&) = delete;

	LUMIX_FORCE_INLINE u8* alloc(u32 size);
	LUMIX_FORCE_INLINE void submitCached();
	
	template <typename T>
	LUMIX_FORCE_INLINE void write(Instruction instruction, const T& val) {
		u8* ptr = alloc(sizeof(val) + sizeof(Instruction));
		memcpy(ptr, &instruction, sizeof(instruction));
		memcpy(ptr + sizeof(instruction), &val, sizeof(val));
	}

	Renderer& renderer;
	PageAllocator& allocator;
	Page* first = nullptr;
	Page* current = nullptr;
	bool run_called = false;
	struct Cache {
		gpu::BindGroupHandle group0;
		gpu::ProgramHandle program;
		gpu::BufferHandle index_buffer;
		struct VertexBuffer {
			gpu::BufferHandle buffer;
			u32 offset;
			u32 stride;
		} vertex_buffers[2];
	};

	struct CacheEx : Cache {
		gpu::BindGroupHandle group1;
		gpu::BufferHandle indirect_buffer;
		u32 dirty = 0;
	};
	CacheEx m_cache;
};

template <typename F>
void DrawStream::pushLambda(const F& f) {
	u8* payload = pushFunction([](void* user_ptr){
		F* f = (F*)user_ptr;
		(*f)();
		f->~F();
	}, sizeof(F));
	new (NewPlaceholder(), payload) F(f);
}

} // namespace Lumix
