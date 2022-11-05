#include "gpu.h"
#include "engine/page_allocator.h"
#include "engine/array.h"
#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/sync.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/stream.h"
#include "engine/string.h"

namespace Lumix {

namespace gpu {

u32 VertexDecl::getStride() const {
	u32 stride = 0;
	for (u32 i = 0; i < attributes_count; ++i) {
		stride += attributes[i].components_count * getSize(attributes[i].type);
	}
	return stride;
}

void VertexDecl::computeHash() {
	hash = RuntimeHash32(attributes, sizeof(Attribute) * attributes_count);
}

void VertexDecl::addAttribute(u8 idx, u8 byte_offset, u8 components_num, AttributeType type, u8 flags)
{
	if(attributes_count >= lengthOf(attributes)) {
		ASSERT(false);
		return;
	}

	Attribute& attr = attributes[attributes_count];
	attr.components_count = components_num;
	attr.idx = idx;
	attr.flags = flags;
	attr.type = type;
	attr.byte_offset = byte_offset;
	++attributes_count;
	hash = RuntimeHash32(attributes, sizeof(Attribute) * attributes_count);
}

int getSize(AttributeType type)
{
	switch(type) {
		case AttributeType::FLOAT: return 4;
		case AttributeType::I8: return 1;
		case AttributeType::U8: return 1;
		case AttributeType::I16: return 2;
		default: ASSERT(false); return 0;
	}
}

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
	u8 data[PageAllocator::PAGE_SIZE - sizeof(Header)];
	Header header;
};

static_assert(sizeof(Encoder::Page) == PageAllocator::PAGE_SIZE);

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

struct BinderShaderBufferData {
	BufferHandle buffer;
	u32 binding_idx;
	BindShaderBufferFlags flags;
};

void Encoder::bindShaderBuffer(BufferHandle buffer, u32 binding_idx, BindShaderBufferFlags flags) {
	BinderShaderBufferData data = {buffer, binding_idx, flags};
	write(Instruction::BIND_SHADER_BUFFER, data);
}

void Encoder::destroy(TextureHandle texture) {
	write(Instruction::DESTROY_TEXTURE, texture);
}

void Encoder::destroy(ProgramHandle program) {
	write(Instruction::DESTROY_PROGRAM, program);
}

void Encoder::destroy(BufferHandle buffer) {
	write(Instruction::DESTROY_BUFFER, buffer);
}

struct ReadTextureData {
	TextureHandle texture;
	u32 mip;
	Span<u8> buf;
};

void Encoder::readTexture(TextureHandle texture, u32 mip, Span<u8> buf) {
	ReadTextureData data = {texture, mip, buf};
	write(Instruction::READ_TEXTURE, data);
}

struct CopyTextureData {
	TextureHandle dst;
	TextureHandle src;
	u32 dst_x;
	u32 dst_y;
};

void Encoder::copy(TextureHandle dst, TextureHandle src, u32 dst_x, u32 dst_y) {
	CopyTextureData data = {dst, src, dst_x, dst_y};
	write(Instruction::COPY_TEXTURE, data);
};

struct CopyBufferData {
	BufferHandle dst;
	BufferHandle src;
	u32 dst_offset;
	u32 src_offset;
	u32 size;
};

void Encoder::copy(BufferHandle dst, BufferHandle src, u32 dst_offset, u32 src_offset, u32 size) {
	CopyBufferData data = {dst, src, dst_offset, src_offset, size};
	write(Instruction::COPY_BUFFER, data);
}

struct CreateBufferData {
	BufferHandle buffer;
	BufferFlags flags;
	size_t size;
	const void* data;
};

void Encoder::createBuffer(BufferHandle buffer, BufferFlags flags, size_t size, const void* ptr) {
	CreateBufferData data = {buffer, flags, size, ptr};
	write(Instruction::CREATE_BUFFER, data);
}

struct CreateTextureData {
	TextureHandle handle;
	u32 w;
	u32 h;
	u32 depth;
	TextureFormat format;
	TextureFlags flags;
	const char* debug_name;
};

void Encoder::createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, TextureFlags flags, const char* debug_name) {
	CreateTextureData data = {handle, w, h, depth, format, flags, debug_name};
	write(Instruction::CREATE_TEXTURE, data);
}

struct BindImageTextureData {
	TextureHandle texture;
	u32 unit;
};

void Encoder::bindImageTexture(TextureHandle texture, u32 unit) {
	BindImageTextureData data = { texture, unit };
	write(Instruction::BIND_IMAGE_TEXTURE, data);
}


void Encoder::dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z) {
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

struct ClearData {
	ClearFlags flags;
	Vec4 color;
	float depth;
};

#define WRITE(val) memcpy(data, &val, sizeof(val)); data += sizeof(val);
#define WRITE_CONST(v) do { auto val = v; memcpy(data, &val, sizeof(val)); data += sizeof(val); } while(false)
#define WRITE_ARRAY(val, count) memcpy(data, val, sizeof(val[0]) * count); data += sizeof(val[0]) * count;

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

void Encoder::createProgram(ProgramHandle prog, const VertexDecl& decl, const char** srcs, const ShaderType* types, u32 num, const char** prefixes, u32 prefixes_count, const char* name) {
	CreateProgramData* data = LUMIX_NEW(getAllocator(), CreateProgramData)(getAllocator());
	data->program = prog;
	data->decl = decl;
	data->sources.reserve(num);
	data->srcs.resize(num);
	data->types.resize(num);
	for (u32 i = 0; i < num; ++i) {
		data->sources.emplace(srcs[i], getAllocator());
		data->srcs[i] = data->sources[i].c_str();
		data->types[i] = types[i];
	}

	data->prefixes.reserve(prefixes_count);
	data->prfxs.resize(prefixes_count);
	for (u32 i = 0; i < prefixes_count; ++i) {
		data->prefixes.emplace(prefixes[i], getAllocator());
		data->prfxs[i] = data->prefixes[i].c_str();
	}
	data->name = name;
	write(Instruction::CREATE_PROGRAM, data);
}

void Encoder::pushDebugGroup(const char* msg) {
	write(Instruction::PUSH_DEBUG_GROUP, msg);
}

struct CreateTextureViewData {
	TextureHandle view;
	TextureHandle texture;
};

void Encoder::createTextureView(TextureHandle view, TextureHandle texture) {
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

void Encoder::generateMipmaps(TextureHandle texture) {
	write(Instruction::GENERATE_MIPMAPS, texture);
}

void Encoder::clear(ClearFlags flags, const float* color, float depth) {
	const ClearData data = { flags, Vec4(color[0], color[1], color[2], color[3]), depth };
	write(Instruction::CLEAR, data);
}

void Encoder::setState(StateFlags state) {
	write(Instruction::SET_STATE, state);
}

void Encoder::bindIndexBuffer(BufferHandle buffer) {
	write(Instruction::BIND_INDEX_BUFFER, buffer);
}

void Encoder::useProgram(ProgramHandle program) {
	write(Instruction::USE_PROGRAM, program);
}

void Encoder::setCurrentWindow(void* window_handle) {
	write(Instruction::SET_CURRENT_WINDOW, window_handle);
}

struct BindVertexBufferData {
	u8 binding_idx;
	BufferHandle buffer;
	u32 offset;
	u32 stride;
};

void Encoder::bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride) {
	const BindVertexBufferData data = {
		(u8)binding_idx, buffer, buffer_offset, stride
	};

	write(Instruction::BIND_VERTEX_BUFFER, data);
}

void Encoder::scissor(u32 x,u32 y,u32 w,u32 h) {
	IVec4 vec(x, y, w, h);
	write(Instruction::SCISSOR, vec);
}

struct DrawIndexedData {
	PrimitiveType primitive_type;
	u32 offset;
	u32 count;
	DataType type;
};

void Encoder::drawIndexed(PrimitiveType primitive_type, u32 offset, u32 count, DataType type) {
	const DrawIndexedData data = { primitive_type, offset, count, type };
	write(Instruction::DRAW_INDEXED, data);
}

struct DrawIndexedInstancedDat {
	PrimitiveType primitive_type;
	u32 indices_count;
	u32 instances_count;
	DataType index_type;
};

void Encoder::drawIndexedInstanced(PrimitiveType primitive_type, u32 indices_count, u32 instances_count, DataType index_type) {
	const DrawIndexedInstancedDat data = { primitive_type, indices_count, instances_count, index_type };
	write(Instruction::DRAW_INDEXED_INSTANCED, data);
}

void Encoder::bindIndirectBuffer(BufferHandle buffer) {
	write(Instruction::BIND_IDIRECT_BUFFER, buffer);
}

struct DrawIndirectData {
	DataType index_type;
	u32 indirect_buffer_offset;
};

void Encoder::drawIndirect(DataType index_type, u32 indirect_buffer_offset) {
	DrawIndirectData data = { index_type, indirect_buffer_offset };
	write(Instruction::DRAW_INDIRECT, data);
}

struct MemoryBarrierData {
	gpu::MemoryBarrierType type;
	BufferHandle buffer;
};

void Encoder::memoryBarrier(gpu::MemoryBarrierType type, BufferHandle buffer) {
	MemoryBarrierData data = {type, buffer};
	write(Instruction::MEMORY_BARRIER, data);
}

struct DrawArraysInstancedData {
	PrimitiveType primitive_type;
	u32 indices_count;
	u32 instances_count;
};

void Encoder::drawArraysInstanced(PrimitiveType type, u32 indices_count, u32 instances_count) {
	const DrawArraysInstancedData data = { type, indices_count, instances_count };
	write(Instruction::DRAW_ARRAYS_INSTANCED, data);
}

struct BindUniformBufferData {
	u32 ub_index;
	BufferHandle buffer;
	size_t offset;
	size_t size;
};

void Encoder::bindUniformBuffer(u32 ub_index, BufferHandle buffer, size_t offset, size_t size) {
	const BindUniformBufferData data = { ub_index, buffer, offset, size };
	write(Instruction::BIND_UNIFORM_BUFFER, data);
}

struct SetFramebufferCubeData {
	TextureHandle cube;
	u32 face;
	u32 mip;
};

void Encoder::setFramebufferCube(TextureHandle cube, u32 face, u32 mip) {
	SetFramebufferCubeData data = {cube, face, mip};
	write(Instruction::SET_FRAMEBUFFER_CUBE, data);
}

void Encoder::setFramebuffer(const TextureHandle* attachments, u32 num, TextureHandle ds, FramebufferFlags flags) {
	u8* data = alloc(sizeof(Instruction) + sizeof(TextureHandle) * (num + 1) + sizeof(u32) + sizeof(FramebufferFlags));

	WRITE_CONST(Instruction::SET_FRAMEBUFFER);
	WRITE(num);
	WRITE(ds);
	WRITE(flags);
	WRITE_ARRAY(attachments, num);
}

void Encoder::bindTextures(const TextureHandle* handles, u32 offset, u32 count) {
	u8* data = alloc(sizeof(Instruction) + sizeof(u32) * 2 + sizeof(TextureHandle) * count);
	
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

void Encoder::update(TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, TextureFormat format, const void* buf, u32 size) {
	UpdateTextureData data = {texture, mip, x, y, z, w, h, format, buf, size};
	write(Instruction::UPDATE_TEXTURE, data);
}

struct UpdateBufferData {
	BufferHandle buffer;
	const void* data;
	size_t size;
};

void Encoder::update(BufferHandle buffer, const void* data, size_t size) {
	UpdateBufferData tmp = {buffer, data, size};
	write(Instruction::UPDATE_BUFFER, tmp);
}

struct DrawArraysData {
	PrimitiveType type;
	u32 offset;
	u32 count;
};

void Encoder::drawArrays(PrimitiveType type, u32 offset, u32 count) {
	DrawArraysData data = { type, offset, count };
	write(Instruction::DRAW_ARRAYS, data);
}

struct DeleteMemoryData {
	void* ptr;
	IAllocator* allocator;
};

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

void Encoder::run() {
	PROFILE_FUNCTION();

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
				case Instruction::SET_STATE: {
					READ(StateFlags, state);
					gpu::setState(state);
					break;
				}
				case Instruction::BIND_IDIRECT_BUFFER: {
					READ(BufferHandle, buffer);
					gpu::bindIndirectBuffer(buffer);
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
					READ(TextureHandle, tex);
					gpu::generateMipmaps(tex);
					break;
				}
				case Instruction::CREATE_PROGRAM: {
					READ(CreateProgramData*, data);
					gpu::createProgram(data->program, data->decl, data->srcs.begin(), data->types.begin(), data->sources.size(), data->prfxs.begin(), data->prfxs.size(), data->name.c_str());
					LUMIX_DELETE(getAllocator(), data);
					break;
				}
				case Instruction::SET_FRAMEBUFFER_CUBE: {
					READ(SetFramebufferCubeData, data);
					gpu::setFramebufferCube(data.cube, data.face, data.mip);
					break;
				}
				case Instruction::SET_FRAMEBUFFER: {
					READ(u32, num);
					READ(TextureHandle, ds);
					READ(FramebufferFlags, flags);
					gpu::setFramebuffer((const TextureHandle*)ptr, num, ds, flags);
					ptr += sizeof(TextureHandle) * num;
					break;
				}
				case Instruction::BIND_TEXTURES: {
					READ(u32, offset);
					READ(u32, count);
					gpu::bindTextures((const TextureHandle*)ptr, offset, count);
					ptr += sizeof(TextureHandle) * count;
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
				case Instruction::BIND_VERTEX_BUFFER: {
					READ(BindVertexBufferData, data);
					gpu::bindVertexBuffer(data.binding_idx, data.buffer, data.offset, data.stride);
					break;
				}
				case Instruction::DRAW_ARRAYS: {
					READ(DrawArraysData, data);
					gpu::drawArrays(data.type, data.offset, data.count);
					break;
				}
				case Instruction::DRAW_INDEXED_INSTANCED: {
					READ(DrawIndexedInstancedDat, data);
					gpu::drawIndexedInstanced(data.primitive_type, data.indices_count, data.instances_count, data.index_type);
					break;
				}
				case Instruction::DRAW_ARRAYS_INSTANCED: {
					READ(DrawArraysInstancedData, data);
					gpu::drawArraysInstanced(data.primitive_type, data.indices_count, data.instances_count);
					break;
				}
				case Instruction::DRAW_INDEXED: {
					READ(DrawIndexedData, data);
					gpu::drawIndexed(data.primitive_type, data.offset, data.count, data.type);
					break;
				}
				case Instruction::BIND_INDEX_BUFFER: {
					READ(BufferHandle, buffer);
					gpu::bindIndexBuffer(buffer);
					break;
				}
				case Instruction::USE_PROGRAM: {
					READ(ProgramHandle, program);
					gpu::useProgram(program);
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
					gpu::createTexture(data.handle, data.w, data.h, data.depth, data.format, data.flags, data.debug_name);
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
					READ(TextureHandle, texture);
					gpu::destroy(texture);
					break;
				}
				case Instruction::DESTROY_PROGRAM: {
					READ(ProgramHandle, program);
					gpu::destroy(program);
					break;
				}
				case Instruction::DESTROY_BUFFER: {
					READ(BufferHandle, buffer);
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

} // namespace gpu

} // namespace Lumix
