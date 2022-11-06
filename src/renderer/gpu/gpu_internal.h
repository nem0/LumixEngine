#pragma once

#include "gpu.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/string.h"
#include "engine/page_allocator.h"

namespace Lumix::gpu {

enum class Instruction : u8 {
	END,
	SET_STATE,
	BIND_INDEX_BUFFER,
	USE_PROGRAM,
	BIND_VERTEX_BUFFER,
	SCISSOR,
	DRAW_INDEXED,
	BIND_TEXTURES,
	CLEAR,
	VIEWPORT,
	BIND_UNIFORM_BUFFER,
	SET_FRAMEBUFFER,
	SET_FRAMEBUFFER_CUBE,
	SET_CURRENT_WINDOW,
	CREATE_PROGRAM,
	DRAW_ARRAYS,
	PUSH_DEBUG_GROUP,
	POP_DEBUG_GROUP,
	DRAW_ARRAYS_INSTANCED,
	DRAW_INDEXED_INSTANCED,
	MEMORY_BARRIER,
	BIND_IDIRECT_BUFFER,
	DRAW_INDIRECT,
	BIND_SHADER_BUFFER,
	DISPATCH,
	CREATE_BUFFER,
	CREATE_TEXTURE,
	BIND_IMAGE_TEXTURE,
	COPY_TEXTURE,
	COPY_BUFFER,
	READ_TEXTURE,
	DESTROY_TEXTURE,
	DESTROY_BUFFER,
	DESTROY_PROGRAM,
	GENERATE_MIPMAPS,
	UPDATE_TEXTURE,
	UPDATE_BUFFER,
	FREE_MEMORY,
	FREE_ALIGNED_MEMORY,
	START_CAPTURE,
	STOP_CAPTURE,
	CREATE_TEXTURE_VIEW
};

struct Encoder::Page {
	struct Header {
		Page* next = nullptr;
		u32 size = 0;
	};
	u8 data[4096 - sizeof(Header)];
	Header header;
};


struct UpdateBufferData {
	BufferHandle buffer;
	const void* data;
	size_t size;
};

struct UpdateTextureData {
	TextureHandle texture;
	u32 mip;
	u32 x;
	u32 y;
	u32 z;
	u32 w;
	u32 h;
	TextureFormat format;
	const void* buf;
	u32 size;
};

struct SetFramebufferCubeData {
	TextureHandle cube;
	u32 face;
	u32 mip;
};

struct BindUniformBufferData {
	u32 ub_index;
	BufferHandle buffer;
	size_t offset;
	size_t size;
};

struct CreateTextureViewData {
	TextureHandle view;
	TextureHandle texture;
};

struct BindVertexBufferData {
	u8 binding_idx;
	BufferHandle buffer;
	u32 offset;
	u32 stride;
};

struct DrawIndexedData {
	PrimitiveType primitive_type;
	u32 offset;
	u32 count;
	DataType type;
};

struct DrawIndexedInstancedDat {
	PrimitiveType primitive_type;
	u32 indices_count;
	u32 instances_count;
	DataType index_type;
};
struct DrawIndirectData {
	DataType index_type;
	u32 indirect_buffer_offset;
};
struct MemoryBarrierData {
	gpu::MemoryBarrierType type;
	BufferHandle buffer;
};

struct ReadTextureData {
	TextureHandle texture;
	u32 mip;
	Span<u8> buf;
};

struct CopyTextureData {
	TextureHandle dst;
	TextureHandle src;
	u32 dst_x;
	u32 dst_y;
};

struct CopyBufferData {
	BufferHandle dst;
	BufferHandle src;
	u32 dst_offset;
	u32 src_offset;
	u32 size;
};

struct CreateBufferData {
	BufferHandle buffer;
	BufferFlags flags;
	size_t size;
	const void* data;
};

struct CreateTextureData {
	TextureHandle handle;
	u32 w;
	u32 h;
	u32 depth;
	TextureFormat format;
	TextureFlags flags;
};

struct BindImageTextureData {
	TextureHandle texture;
	u32 unit;
};

struct ClearData {
	ClearFlags flags;
	Vec4 color;
	float depth;
};

struct DrawArraysInstancedData {
	PrimitiveType primitive_type;
	u32 indices_count;
	u32 instances_count;
};

struct DeleteMemoryData {
	void* ptr;
	IAllocator* allocator;
};

struct BinderShaderBufferData {
	BufferHandle buffer;
	u32 binding_idx;
	BindShaderBufferFlags flags;
};

struct CreateProgramData {
	CreateProgramData(IAllocator& allocator) 
		: sources(allocator)
		, prefixes(allocator)
		, srcs(allocator)
		, prfxs(allocator)
		, types(allocator)
		, name(allocator)
	{}

	ProgramHandle program;
	VertexDecl decl;
	Array<String> sources;
	Array<const char*> srcs;
	Array<String> prefixes;
	Array<const char*> prfxs;
	Array<ShaderType> types;
	String name;
};

struct DrawArraysData {
	PrimitiveType type;
	u32 offset;
	u32 count;
};

static_assert(sizeof(Encoder::Page) == PageAllocator::PAGE_SIZE);

} // namespace Lumix::gpu
