#include "gpu.h"
#include "gpu_internal.h"
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

void Encoder::readTexture(TextureHandle texture, u32 mip, Span<u8> buf) {
	ReadTextureData data = {texture, mip, buf};
	write(Instruction::READ_TEXTURE, data);
}

void Encoder::copy(TextureHandle dst, TextureHandle src, u32 dst_x, u32 dst_y) {
	CopyTextureData data = {dst, src, dst_x, dst_y};
	write(Instruction::COPY_TEXTURE, data);
};

void Encoder::copy(BufferHandle dst, BufferHandle src, u32 dst_offset, u32 src_offset, u32 size) {
	CopyBufferData data = {dst, src, dst_offset, src_offset, size};
	write(Instruction::COPY_BUFFER, data);
}

void Encoder::createBuffer(BufferHandle buffer, BufferFlags flags, size_t size, const void* ptr) {
	CreateBufferData data = {buffer, flags, size, ptr};
	write(Instruction::CREATE_BUFFER, data);
}

void Encoder::createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, TextureFlags flags, const char* debug_name) {
	ASSERT(debug_name);
	CreateTextureData data = {handle, w, h, depth, format, flags};
	write(Instruction::CREATE_TEXTURE, data);
	const u32 len = stringLength(debug_name) + 1;
	u8* ptr = alloc(sizeof(len) + len);
	memcpy(ptr, &len, sizeof(len));
	memcpy(ptr + sizeof(len), debug_name, len);
}

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

#define WRITE(val) memcpy(data, &val, sizeof(val)); data += sizeof(val);
#define WRITE_CONST(v) do { auto val = v; memcpy(data, &val, sizeof(val)); data += sizeof(val); } while(false)
#define WRITE_ARRAY(val, count) memcpy(data, val, sizeof(val[0]) * count); data += sizeof(val[0]) * count;

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

void Encoder::drawIndexed(PrimitiveType primitive_type, u32 offset, u32 count, DataType type) {
	const DrawIndexedData data = { primitive_type, offset, count, type };
	write(Instruction::DRAW_INDEXED, data);
}

void Encoder::drawIndexedInstanced(PrimitiveType primitive_type, u32 indices_count, u32 instances_count, DataType index_type) {
	const DrawIndexedInstancedDat data = { primitive_type, indices_count, instances_count, index_type };
	write(Instruction::DRAW_INDEXED_INSTANCED, data);
}

void Encoder::bindIndirectBuffer(BufferHandle buffer) {
	write(Instruction::BIND_IDIRECT_BUFFER, buffer);
}


void Encoder::drawIndirect(DataType index_type, u32 indirect_buffer_offset) {
	DrawIndirectData data = { index_type, indirect_buffer_offset };
	write(Instruction::DRAW_INDIRECT, data);
}


void Encoder::memoryBarrier(gpu::MemoryBarrierType type, BufferHandle buffer) {
	MemoryBarrierData data = {type, buffer};
	write(Instruction::MEMORY_BARRIER, data);
}

void Encoder::drawArraysInstanced(PrimitiveType type, u32 indices_count, u32 instances_count) {
	const DrawArraysInstancedData data = { type, indices_count, instances_count };
	write(Instruction::DRAW_ARRAYS_INSTANCED, data);
}

void Encoder::bindUniformBuffer(u32 ub_index, BufferHandle buffer, size_t offset, size_t size) {
	const BindUniformBufferData data = { ub_index, buffer, offset, size };
	write(Instruction::BIND_UNIFORM_BUFFER, data);
}

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

void Encoder::update(TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, TextureFormat format, const void* buf, u32 size) {
	UpdateTextureData data = {texture, mip, x, y, z, w, h, format, buf, size};
	write(Instruction::UPDATE_TEXTURE, data);
}

void Encoder::update(BufferHandle buffer, const void* data, size_t size) {
	UpdateBufferData tmp = {buffer, data, size};
	write(Instruction::UPDATE_BUFFER, tmp);
}

void Encoder::drawArrays(PrimitiveType type, u32 offset, u32 count) {
	DrawArraysData data = { type, offset, count };
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

} // namespace gpu

} // namespace Lumix
