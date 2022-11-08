#include "encoder.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/page_allocator.h"
#include "engine/string.h"
#include <intrin.h>

namespace Lumix {


struct Encoder::Page {
	struct Header {
		Page* next = nullptr;
		u32 size = 0;
	};
	u8 data[4096 - sizeof(Header)];
	Header header;
};

enum class Encoder::Instruction : u8 {
	END,
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
	CREATE_TEXTURE_VIEW,
	BIND,
	DIRTY_CACHE
};

namespace {

namespace Dirty {
	enum : u32 {
		PROGRAM = 0b11 << 2,
		INDEX_BUFFER = 0b11 << 4,
		INDIRECT_BUFFER = 0b11 << 6,
		VERTEX_BUFFER0 = 0b1111 << 8,
		VERTEX_BUFFER1 = 0b1111 << 12,

		BIND = PROGRAM | INDEX_BUFFER | VERTEX_BUFFER0 | VERTEX_BUFFER1
	};
}

struct UpdateBufferData {
	gpu::BufferHandle buffer;
	const void* data;
	size_t size;
};

struct UpdateTextureData {
	gpu::TextureHandle texture;
	u32 mip;
	u32 x;
	u32 y;
	u32 z;
	u32 w;
	u32 h;
	gpu::TextureFormat format;
	const void* buf;
	u32 size;
};

struct SetFramebufferCubeData {
	gpu::TextureHandle cube;
	u32 face;
	u32 mip;
};

struct BindUniformBufferData {
	u32 ub_index;
	gpu::BufferHandle buffer;
	size_t offset;
	size_t size;
};

struct CreateTextureViewData {
	gpu::TextureHandle view;
	gpu::TextureHandle texture;
};

struct DrawIndexedData {
	u32 offset;
	u32 count;
	gpu::DataType type;
};

struct DrawIndexedInstancedDat {
	u32 indices_count;
	u32 instances_count;
	gpu::DataType index_type;
};
struct DrawIndirectData {
	gpu::DataType index_type;
	u32 indirect_buffer_offset;
};
struct MemoryBarrierData {
	gpu::MemoryBarrierType type;
	gpu::BufferHandle buffer;
};

struct ReadTextureData {
	gpu::TextureHandle texture;
	u32 mip;
	Span<u8> buf;
};

struct CopyTextureData {
	gpu::TextureHandle dst;
	gpu::TextureHandle src;
	u32 dst_x;
	u32 dst_y;
};

struct CopyBufferData {
	gpu::BufferHandle dst;
	gpu::BufferHandle src;
	u32 dst_offset;
	u32 src_offset;
	u32 size;
};

struct CreateBufferData {
	gpu::BufferHandle buffer;
	gpu::BufferFlags flags;
	size_t size;
	const void* data;
};

struct CreateTextureData {
	gpu::TextureHandle handle;
	u32 w;
	u32 h;
	u32 depth;
	gpu::TextureFormat format;
	gpu::TextureFlags flags;
};

struct BindImageTextureData {
	gpu::TextureHandle texture;
	u32 unit;
};

struct ClearData {
	gpu::ClearFlags flags;
	Vec4 color;
	float depth;
};

struct DrawArraysInstancedData {
	u32 indices_count;
	u32 instances_count;
};

struct DeleteMemoryData {
	void* ptr;
	IAllocator* allocator;
};

struct BinderShaderBufferData {
	gpu::BufferHandle buffer;
	u32 binding_idx;
	gpu::BindShaderBufferFlags flags;
};

struct CreateProgramData {
	CreateProgramData(IAllocator& allocator) 
		: sources(allocator)
		, prefixes(allocator)
		, srcs(allocator)
		, prfxs(allocator)
		, types(allocator)
		, name(allocator)
		, decl(gpu::PrimitiveType::NONE)
	{}

	gpu::ProgramHandle program;
	gpu::StateFlags state;
	gpu::VertexDecl decl;
	Array<String> sources;
	Array<const char*> srcs;
	Array<String> prefixes;
	Array<const char*> prfxs;
	Array<gpu::ShaderType> types;
	String name;
};

struct DrawArraysData {
	u32 offset;
	u32 count;
};

static_assert(sizeof(Encoder::Page) == PageAllocator::PAGE_SIZE);

} // anonymous namespace

Encoder::~Encoder() {
	allocator.lock();
	while (first) {
		Page* next = first->header.next;
		allocator.deallocate(first, false);
		first = next;
	}
	allocator.unlock();
}

Encoder::Encoder(Encoder&& rhs)
	: allocator(rhs.allocator)
{
	first = rhs.first;
	current = rhs.current;
	rhs.first = rhs.current = nullptr;
}

Encoder::Encoder(PageAllocator& allocator)
	: allocator(allocator)
{
	first = new (NewPlaceholder(), allocator.allocate(true)) Page;
	current = first;
}

void Encoder::bindShaderBuffer(gpu::BufferHandle buffer, u32 binding_idx, gpu::BindShaderBufferFlags flags) {
	BinderShaderBufferData data = {buffer, binding_idx, flags};
	write(Instruction::BIND_SHADER_BUFFER, data);
}

void Encoder::destroy(gpu::TextureHandle texture) {
	write(Instruction::DESTROY_TEXTURE, texture);
}

void Encoder::destroy(gpu::ProgramHandle program) {
	write(Instruction::DESTROY_PROGRAM, program);
}

void Encoder::destroy(gpu::BufferHandle buffer) {
	write(Instruction::DESTROY_BUFFER, buffer);
}

void Encoder::readTexture(gpu::TextureHandle texture, u32 mip, Span<u8> buf) {
	ReadTextureData data = {texture, mip, buf};
	write(Instruction::READ_TEXTURE, data);
}

void Encoder::copy(gpu::TextureHandle dst, gpu::TextureHandle src, u32 dst_x, u32 dst_y) {
	CopyTextureData data = {dst, src, dst_x, dst_y};
	write(Instruction::COPY_TEXTURE, data);
};

void Encoder::copy(gpu::BufferHandle dst, gpu::BufferHandle src, u32 dst_offset, u32 src_offset, u32 size) {
	CopyBufferData data = {dst, src, dst_offset, src_offset, size};
	write(Instruction::COPY_BUFFER, data);
}

void Encoder::createBuffer(gpu::BufferHandle buffer, gpu::BufferFlags flags, size_t size, const void* ptr) {
	CreateBufferData data = {buffer, flags, size, ptr};
	write(Instruction::CREATE_BUFFER, data);
}

void Encoder::createTexture(gpu::TextureHandle handle, u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const char* debug_name) {
	ASSERT(debug_name);
	CreateTextureData data = {handle, w, h, depth, format, flags};
	write(Instruction::CREATE_TEXTURE, data);
	const u32 len = stringLength(debug_name) + 1;
	u8* ptr = alloc(sizeof(len) + len);
	memcpy(ptr, &len, sizeof(len));
	memcpy(ptr + sizeof(len), debug_name, len);
}

void Encoder::bindImageTexture(gpu::TextureHandle texture, u32 unit) {
	BindImageTextureData data = { texture, unit };
	write(Instruction::BIND_IMAGE_TEXTURE, data);
}

void Encoder::dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z) {
	submitCached();
	const IVec3 v(num_groups_x, num_groups_y, num_groups_z);
	write(Instruction::DISPATCH, v);
}

void Encoder::merge(Encoder& rhs) {
	ASSERT(&allocator == &rhs.allocator);
	
	const Instruction end_instr = Instruction::END;
	if (!run_called) memcpy(current->data + current->header.size, &end_instr, sizeof(end_instr));
	run_called = rhs.run_called;
	current->header.next = rhs.first;
	current = rhs.current;
	rhs.first = rhs.current = nullptr;
}

u8* Encoder::alloc(u32 size) {
	u32 start = current->header.size;
	if (start + size > sizeof(current->data) - sizeof(Instruction)) {
		const Instruction end_instr = Instruction::END;
		memcpy(current->data + current->header.size, &end_instr, sizeof(end_instr));
		
		Page* new_page = new (NewPlaceholder(), allocator.allocate(true)) Page;
		current->header.next = new_page;
		current = new_page;
		start = 0;
		ASSERT(size < sizeof(current->data));
	}
	current->header.size += size;
	return current->data + start;
}

#define WRITE(val) memcpy(data, &val, sizeof(val)); data += sizeof(val);
#define WRITE_CONST(v) do { auto val = v; memcpy(data, &val, sizeof(val)); data += sizeof(val); } while(false)
#define WRITE_ARRAY(val, count) memcpy(data, val, sizeof(val[0]) * count); data += sizeof(val[0]) * count;

void Encoder::createProgram(gpu::ProgramHandle prog
	, gpu::StateFlags state
	, const gpu::VertexDecl& decl
	, const char** srcs
	, const gpu::ShaderType* types
	, u32 num
	, const char** prefixes
	, u32 prefixes_count
	, const char* name
) {
	CreateProgramData* data = LUMIX_NEW(gpu::getAllocator(), CreateProgramData)(gpu::getAllocator());
	data->program = prog;
	data->state = state;
	data->decl = decl;
	data->sources.reserve(num);
	data->srcs.resize(num);
	data->types.resize(num);
	for (u32 i = 0; i < num; ++i) {
		data->sources.emplace(srcs[i], gpu::getAllocator());
		data->srcs[i] = data->sources[i].c_str();
		data->types[i] = types[i];
	}

	data->prefixes.reserve(prefixes_count);
	data->prfxs.resize(prefixes_count);
	for (u32 i = 0; i < prefixes_count; ++i) {
		data->prefixes.emplace(prefixes[i], gpu::getAllocator());
		data->prfxs[i] = data->prefixes[i].c_str();
	}
	data->name = name;
	write(Instruction::CREATE_PROGRAM, data);
}

void Encoder::pushDebugGroup(const char* msg) {
	write(Instruction::PUSH_DEBUG_GROUP, msg);
}

void Encoder::createTextureView(gpu::TextureHandle view, gpu::TextureHandle texture) {
	CreateTextureViewData data = {view, texture};
	write(Instruction::CREATE_TEXTURE_VIEW, data);
}

void Encoder::startCapture() {
	u8* ptr = alloc(sizeof(Instruction));
	const Instruction instruction = Instruction::START_CAPTURE;
	memcpy(ptr, &instruction, sizeof(instruction));
}

void Encoder::stopCapture() {
	u8* ptr = alloc(sizeof(Instruction));
	const Instruction instruction = Instruction::STOP_CAPTURE;
	memcpy(ptr, &instruction, sizeof(instruction));
}

void Encoder::popDebugGroup() {
	u8* ptr = alloc(sizeof(Instruction));
	const Instruction instruction = Instruction::POP_DEBUG_GROUP;
	memcpy(ptr, &instruction, sizeof(instruction));
}

void Encoder::generateMipmaps(gpu::TextureHandle texture) {
	write(Instruction::GENERATE_MIPMAPS, texture);
}

void Encoder::clear(gpu::ClearFlags flags, const float* color, float depth) {
	const ClearData data = { flags, Vec4(color[0], color[1], color[2], color[3]), depth };
	write(Instruction::CLEAR, data);
}

void Encoder::bindIndexBuffer(gpu::BufferHandle buffer) {
	m_cache.index_buffer = buffer;
	m_cache.dirty |= Dirty::INDEX_BUFFER;
}

void Encoder::useProgram(gpu::ProgramHandle program) {
	m_cache.program = program;
	m_cache.dirty |= Dirty::PROGRAM;
}

void Encoder::setCurrentWindow(void* window_handle) {
	write(Instruction::SET_CURRENT_WINDOW, window_handle);
}

void Encoder::bindVertexBuffer(u32 binding_idx, gpu::BufferHandle buffer, u32 buffer_offset, u32 stride) {
	ASSERT(binding_idx < lengthOf(m_cache.vertex_buffers));
	m_cache.vertex_buffers[binding_idx] = { buffer, buffer_offset, stride};
	m_cache.dirty |= binding_idx == 0 ? Dirty::VERTEX_BUFFER0 : Dirty::VERTEX_BUFFER1;
}

void Encoder::scissor(u32 x,u32 y,u32 w,u32 h) {
	IVec4 vec(x, y, w, h);
	write(Instruction::SCISSOR, vec);
}

void Encoder::drawIndexed(u32 offset, u32 count, gpu::DataType type) {
	submitCached();
	const DrawIndexedData data = { offset, count, type };
	write(Instruction::DRAW_INDEXED, data);
}

void Encoder::drawIndexedInstanced(u32 indices_count, u32 instances_count, gpu::DataType index_type) {
	submitCached();
	const DrawIndexedInstancedDat data = { indices_count, instances_count, index_type };
	write(Instruction::DRAW_INDEXED_INSTANCED, data);
}

void Encoder::bindIndirectBuffer(gpu::BufferHandle buffer) {
	m_cache.indirect_buffer = buffer;
	m_cache.dirty |= Dirty::INDIRECT_BUFFER;
}


void Encoder::drawIndirect(gpu::DataType index_type, u32 indirect_buffer_offset) {
	submitCached();
	DrawIndirectData data = { index_type, indirect_buffer_offset };
	write(Instruction::DRAW_INDIRECT, data);
}


void Encoder::memoryBarrier(gpu::MemoryBarrierType type, gpu::BufferHandle buffer) {
	MemoryBarrierData data = {type, buffer};
	write(Instruction::MEMORY_BARRIER, data);
}

void Encoder::drawArraysInstanced(u32 indices_count, u32 instances_count) {
	submitCached();
	const DrawArraysInstancedData data = { indices_count, instances_count };
	write(Instruction::DRAW_ARRAYS_INSTANCED, data);
}

void Encoder::bindUniformBuffer(u32 ub_index, gpu::BufferHandle buffer, u32 offset, u32 size) {
	const BindUniformBufferData data = { ub_index, buffer, offset, size };
	write(Instruction::BIND_UNIFORM_BUFFER, data);
}

void Encoder::setFramebufferCube(gpu::TextureHandle cube, u32 face, u32 mip) {
	SetFramebufferCubeData data = {cube, face, mip};
	write(Instruction::SET_FRAMEBUFFER_CUBE, data);
}

void Encoder::setFramebuffer(const gpu::TextureHandle* attachments, u32 num, gpu::TextureHandle ds, gpu::FramebufferFlags flags) {
	u8* data = alloc(sizeof(Instruction) + sizeof(gpu::TextureHandle) * (num + 1) + sizeof(u32) + sizeof(gpu::FramebufferFlags));

	WRITE_CONST(Instruction::SET_FRAMEBUFFER);
	WRITE(num);
	WRITE(ds);
	WRITE(flags);
	WRITE_ARRAY(attachments, num);
}

void Encoder::bindTextures(const gpu::TextureHandle* handles, u32 offset, u32 count) {
	u8* data = alloc(sizeof(Instruction) + sizeof(u32) * 2 + sizeof(gpu::TextureHandle) * count);
	
	WRITE_CONST(Instruction::BIND_TEXTURES);
	WRITE(offset);
	WRITE(count);
	WRITE_ARRAY(handles, count);
}

#undef WRITE
#undef WRITE_CONST
#undef WRITE_ARRAY

void Encoder::viewport(u32 x, u32 y, u32 w, u32 h) {
	const IVec4 data(x, y, w, h);
	write(Instruction::VIEWPORT, data);
}

void Encoder::update(gpu::TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, gpu::TextureFormat format, const void* buf, u32 size) {
	UpdateTextureData data = {texture, mip, x, y, z, w, h, format, buf, size};
	write(Instruction::UPDATE_TEXTURE, data);
}

void Encoder::update(gpu::BufferHandle buffer, const void* data, size_t size) {
	UpdateBufferData tmp = {buffer, data, size};
	write(Instruction::UPDATE_BUFFER, tmp);
}

void Encoder::drawArrays(u32 offset, u32 count) {
	submitCached();
	DrawArraysData data = { offset, count };
	write(Instruction::DRAW_ARRAYS, data);
}

void Encoder::freeMemory(void* ptr, IAllocator& allocator) {
	DeleteMemoryData data = { ptr, &allocator };
	write(Instruction::FREE_MEMORY, data);
}

void Encoder::freeAlignedMemory(void* ptr, IAllocator& allocator) {
	DeleteMemoryData data = { ptr, &allocator };
	write(Instruction::FREE_ALIGNED_MEMORY, data);
}

void Encoder::reset() {
	allocator.lock();
	while (first) {
		Page* next = first->header.next;
		allocator.deallocate(first, false);
		first = next;
	}
	first = new (NewPlaceholder(), allocator.allocate(false)) Page;
	allocator.unlock();
	
	current = first;
	run_called = false;
}

void Encoder::submitCached() {
	const u32 dirty = m_cache.dirty;
	if (dirty == 0) return;
	
	m_cache.dirty = 0;
	if (dirty == Dirty::BIND) {
		u8* ptr = alloc(sizeof(Instruction) + sizeof(Cache));
		const Instruction instr = Instruction::BIND;
		memcpy(ptr, &instr, sizeof(instr));
		ptr += sizeof(instr);
		memcpy(ptr, &m_cache, sizeof(Cache));
		return;
	}

	const u32 count = __popcnt(dirty);
	u8* ptr = alloc(sizeof(Instruction) + sizeof(dirty) + count * sizeof(u32));
	Instruction instr = Instruction::DIRTY_CACHE;
	#define WRITE(V) do { memcpy(ptr, &V, sizeof(V)); ptr += sizeof(V); } while(false)
	WRITE(instr);
	WRITE(dirty);
	if (dirty & Dirty::PROGRAM) WRITE(m_cache.program);
	if (dirty & Dirty::INDEX_BUFFER) WRITE(m_cache.index_buffer);
	if (dirty & Dirty::INDIRECT_BUFFER) WRITE(m_cache.indirect_buffer);
	if (dirty & Dirty::VERTEX_BUFFER0) WRITE(m_cache.vertex_buffers[0]);
	if (dirty & Dirty::VERTEX_BUFFER1) WRITE(m_cache.vertex_buffers[1]);
	#undef WRITE
}

void Encoder::run() {
	if (!run_called) {
		const Instruction end_instr = Instruction::END;
		memcpy(current->data + current->header.size, &end_instr, sizeof(end_instr));
		run_called = true;
	}
	
	Page* page = first;
	#define READ(T, N) T N; memcpy(&N, ptr, sizeof(T)); ptr += sizeof(T);
	while (page) {
		const u8* ptr = page->data;
		for (;;) {
			READ(Instruction, instr);
			switch(instr) {
				case Instruction::END: goto next_page;
				case Instruction::BIND: {
					READ(Cache, cache);
					gpu::useProgram(cache.program);
					gpu::bindIndexBuffer(cache.index_buffer);
					gpu::bindVertexBuffer(0, cache.vertex_buffers[0].buffer, cache.vertex_buffers[0].offset, cache.vertex_buffers[0].stride);
					gpu::bindVertexBuffer(1, cache.vertex_buffers[1].buffer, cache.vertex_buffers[1].offset, cache.vertex_buffers[1].stride);
					break;
				}
				case Instruction::DIRTY_CACHE: {
					READ(u32, dirty);
					if (dirty & Dirty::PROGRAM) {
						READ(gpu::ProgramHandle, program);
						gpu::useProgram(program);
					}
					if (dirty & Dirty::INDEX_BUFFER) {
						READ(gpu::BufferHandle, buf);
						gpu::bindIndexBuffer(buf);
					}
					if (dirty & Dirty::INDIRECT_BUFFER) {
						READ(gpu::BufferHandle, buf);
						gpu::bindIndirectBuffer(buf);
					}
					if (dirty & Dirty::VERTEX_BUFFER0) {
						READ(Cache::VertexBuffer, buf);
						gpu::bindVertexBuffer(0, buf.buffer, buf.offset, buf.stride);
					}
					if (dirty & Dirty::VERTEX_BUFFER1) {
						READ(Cache::VertexBuffer, buf);
						gpu::bindVertexBuffer(1, buf.buffer, buf.offset, buf.stride);
					}
					break;
				}
				case Instruction::DRAW_INDIRECT: {
					READ(DrawIndirectData, data);
					gpu::drawIndirect(data.index_type, data.indirect_buffer_offset);
					break;
				}
				case Instruction::MEMORY_BARRIER: {
					READ(MemoryBarrierData, data);
					gpu::memoryBarrier(data.type, data.buffer);
					break;
				}
				case Instruction::POP_DEBUG_GROUP:
					gpu::popDebugGroup();
					break;
				case Instruction::PUSH_DEBUG_GROUP: {
					READ(const char*, msg);
					gpu::pushDebugGroup(msg);
					break;
				}
				case Instruction::UPDATE_BUFFER: {
					READ(UpdateBufferData, data);
					gpu::update(data.buffer, data.data, data.size);
					break;
				}
				case Instruction::UPDATE_TEXTURE: {
					READ(UpdateTextureData, data);
					gpu::update(data.texture, data.mip, data.x, data.y, data.z, data.w, data.h, data.format, data.buf, data.size);
					break;
				}
				case Instruction::BIND_SHADER_BUFFER: {
					READ(BinderShaderBufferData, data);
					gpu::bindShaderBuffer(data.buffer, data.binding_idx, data.flags);
					break;
				}
				case Instruction::GENERATE_MIPMAPS: {
					READ(gpu::TextureHandle, tex);
					gpu::generateMipmaps(tex);
					break;
				}
				case Instruction::CREATE_PROGRAM: {
					READ(CreateProgramData*, data);
					gpu::createProgram(data->program
						, data->state
						, data->decl
						, data->srcs.begin()
						, data->types.begin()
						, data->sources.size()
						, data->prfxs.begin()
						, data->prfxs.size()
						, data->name.c_str()
					);
					LUMIX_DELETE(gpu::getAllocator(), data);
					break;
				}
				case Instruction::SET_FRAMEBUFFER_CUBE: {
					READ(SetFramebufferCubeData, data);
					gpu::setFramebufferCube(data.cube, data.face, data.mip);
					break;
				}
				case Instruction::SET_FRAMEBUFFER: {
					READ(u32, num);
					READ(gpu::TextureHandle, ds);
					READ(gpu::FramebufferFlags, flags);
					gpu::setFramebuffer((const gpu::TextureHandle*)ptr, num, ds, flags);
					ptr += sizeof(gpu::TextureHandle) * num;
					break;
				}
				case Instruction::BIND_TEXTURES: {
					READ(u32, offset);
					READ(u32, count);
					gpu::bindTextures((const gpu::TextureHandle*)ptr, offset, count);
					ptr += sizeof(gpu::TextureHandle) * count;
					break;
				}
				case Instruction::CLEAR: {
					READ(ClearData, data);
					gpu::clear(data.flags, &data.color.x, data.depth);
					break;
				}
				case Instruction::BIND_UNIFORM_BUFFER: {
					READ(BindUniformBufferData, data);
					gpu::bindUniformBuffer(data.ub_index, data.buffer, data.offset, data.size);
					break;
				}
				case Instruction::DRAW_ARRAYS: {
					READ(DrawArraysData, data);
					gpu::drawArrays(data.offset, data.count);
					break;
				}
				case Instruction::DRAW_INDEXED_INSTANCED: {
					READ(DrawIndexedInstancedDat, data);
					gpu::drawIndexedInstanced(data.indices_count, data.instances_count, data.index_type);
					break;
				}
				case Instruction::DRAW_ARRAYS_INSTANCED: {
					READ(DrawArraysInstancedData, data);
					gpu::drawArraysInstanced(data.indices_count, data.instances_count);
					break;
				}
				case Instruction::DRAW_INDEXED: {
					READ(DrawIndexedData, data);
					gpu::drawIndexed(data.offset, data.count, data.type);
					break;
				}
				case Instruction::SET_CURRENT_WINDOW: {
					READ(void*, window_handle);
					gpu::setCurrentWindow(window_handle);
					break;
				}
				case Instruction::SCISSOR: {
					READ(IVec4, vec);
					gpu::scissor(vec.x, vec.y, vec.z, vec.w);
					break;
				}
				case Instruction::CREATE_TEXTURE: {
					READ(CreateTextureData, data);
					READ(u32, len);
					const char* debug_name = (const char*)ptr;
					ptr += len;
					gpu::createTexture(data.handle, data.w, data.h, data.depth, data.format, data.flags, debug_name);
					break;
				}
				case Instruction::CREATE_BUFFER: {
					READ(CreateBufferData, data);
					gpu::createBuffer(data.buffer, data.flags, data.size, data.data);
					break;
				}
				case Instruction::BIND_IMAGE_TEXTURE: {
					READ(BindImageTextureData, data);
					gpu::bindImageTexture(data.texture, data.unit);
					break;
				}
				case Instruction::COPY_TEXTURE: {
					READ(CopyTextureData, data);
					gpu::copy(data.dst, data.src, data.dst_x, data.dst_y);
					break;
				}
				case Instruction::COPY_BUFFER: {
					READ(CopyBufferData, data);
					gpu::copy(data.dst, data.src, data.dst_offset, data.src_offset, data.size);
					break;
				}
				case Instruction::READ_TEXTURE: {
					READ(ReadTextureData, data);
					gpu::readTexture(data.texture, data.mip, data.buf);
					break;
				}
				case Instruction::DESTROY_TEXTURE: {
					READ(gpu::TextureHandle, texture);
					gpu::destroy(texture);
					break;
				}
				case Instruction::DESTROY_PROGRAM: {
					READ(gpu::ProgramHandle, program);
					gpu::destroy(program);
					break;
				}
				case Instruction::DESTROY_BUFFER: {
					READ(gpu::BufferHandle, buffer);
					gpu::destroy(buffer);
					break;
				}
				case Instruction::FREE_MEMORY: {
					READ(DeleteMemoryData, data);
					data.allocator->deallocate(data.ptr);
					break;
				}
				case Instruction::FREE_ALIGNED_MEMORY: {
					READ(DeleteMemoryData, data);
					data.allocator->deallocate_aligned(data.ptr);
					break;
				}
				case Instruction::DISPATCH: {
					READ(IVec3, size);
					gpu::dispatch(size.x, size.y, size.z);
					break;
				}
				case Instruction::START_CAPTURE: {
					gpu::startCapture();
					break;
				}
				case Instruction::STOP_CAPTURE: {
					gpu::stopCapture();
					break;
				}
				case Instruction::CREATE_TEXTURE_VIEW: {
					READ(CreateTextureViewData, data);
					gpu::createTextureView(data.view, data.texture);
					break;
				}
				case Instruction::VIEWPORT: {
					READ(IVec4, vec);
					gpu::viewport(vec.x, vec.y, vec.z, vec.w);
					break;
				}
				default:
					ASSERT(false);
					goto next_page;
			}
		}
		next_page:

		page = page->header.next;
	}
}

} // namespace Lumix