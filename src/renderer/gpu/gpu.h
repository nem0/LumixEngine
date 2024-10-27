#pragma once

#include "core/delegate.h"
#include "core/span.h"
#include "engine/lumix.h"

namespace Lumix {

struct IAllocator;
struct PageAllocator;

namespace gpu {

using BufferHandle = struct Buffer*;
using ProgramHandle = struct Program*;
using TextureHandle = struct Texture*;
using QueryHandle = struct Query*;
const BufferHandle INVALID_BUFFER = nullptr;
const ProgramHandle INVALID_PROGRAM = nullptr;
const TextureHandle INVALID_TEXTURE = nullptr;
const QueryHandle INVALID_QUERY = nullptr;

struct BindlessHandle {
	constexpr BindlessHandle() {}
	explicit BindlessHandle(u32 value) : value(value) {}
	u32 value = 0;
};

static constexpr BindlessHandle INVALID_BINDLESS_HANDLE = {};

struct RWBindlessHandle {
	constexpr RWBindlessHandle() {}
	explicit RWBindlessHandle(u32 value) : value(value) {}
	u32 value = 0;
};

static constexpr RWBindlessHandle INVALID_RW_BINDLESS_HANDLE = {};

enum class InitFlags : u32 {
	NONE = 0,
	DEBUG = 1 << 0,
	// disable dynamic frequency scaling
	// can crash gpu if PC is not in developer mode
	STABLE_POWER_STATE = 1 << 1
};

enum class BarrierType : u8 {
	READ,
	WRITE,
	COMMON,
};

enum class FramebufferFlags : u32 {
	NONE = 0,
	SRGB = 1 << 0,
	READONLY_DEPTH = 1 << 1,
	READONLY_STENCIL = 1 << 2,

	READONLY_DEPTH_STENCIL = READONLY_DEPTH | READONLY_STENCIL,
};

enum class StateFlags : u64 {
	NONE = 0,
	WIREFRAME = 1 << 0,
	DEPTH_FN_GREATER = 1 << 1,
	DEPTH_FN_EQUAL = 1 << 2,
	DEPTH_FUNCTION = DEPTH_FN_GREATER | DEPTH_FN_EQUAL,
	CULL_FRONT = 1 << 3,
	CULL_BACK = 1 << 4,
	DEPTH_WRITE = 1 << 5,

	/* 16 bits reserved for blending*/
	/* 40 bits reserver for stencil*/ 
};

enum class QueryType : u32 {
	TIMESTAMP,
	STATS
};

enum class PrimitiveType : u8 {
	TRIANGLES,
	TRIANGLE_STRIP,
	LINES,
	POINTS,

	NONE
};

enum class ShaderType : u32 {
	COMPUTE,
	SURFACE
};


enum class ClearFlags : u32 {
	COLOR = 1 << 0,
	DEPTH = 1 << 1,
	STENCIL = 1 << 2,

	ALL = COLOR | DEPTH | STENCIL,
};


enum class StencilFuncs : u8 {
	DISABLE,
	ALWAYS,
	EQUAL,
	NOT_EQUAL,
};

enum class StencilOps : u8 {
	KEEP,
	ZERO,
	REPLACE,
	INCR,
	INCR_WRAP,
	DECR,
	DECR_WRAP,
	INVERT
};


enum class BlendFactors : u8 {
	ZERO,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
	SRC1_COLOR,
	ONE_MINUS_SRC1_COLOR,
	SRC1_ALPHA,
	ONE_MINUS_SRC1_ALPHA,
};


enum class AttributeType : u8 {
	U8,
	FLOAT,
	I16,
	I8
};


// keep order, this is serialized
enum class TextureFormat : u32 {
	R8,
	RG8,
	D32,
	D24S8,
	RGBA8,
	RGBA16,
	RGBA16F,
	RGBA32F,
	BGRA8,
	R16F,
	R16,
	R32F,
	RG32F,
	SRGB,
	SRGBA,
	BC1,
	BC2,
	BC3,
	BC4,
	BC5,
	R11G11B10F,
	RGB32F,
	RG16,
	RG16F
};

enum class TextureFlags : u32 {
	NONE = 0,
	NO_MIPS = 1 << 1,
	SRGB = 1 << 2,
	READBACK = 1 << 3,
	IS_3D = 1 << 4,
	IS_CUBE = 1 << 5,
	COMPUTE_WRITE = 1 << 6,
	RENDER_TARGET = 1 << 7
};

enum class BufferFlags : u32 {
	NONE = 0,
	IMMUTABLE = 1 << 0,
	SHADER_BUFFER = 1 << 1,
	MAPPABLE = 1 << 2
};

enum class DataType : u32 {
	U16,
	U32
};

#pragma pack(1)
struct Attribute {
	enum Flags {
		NORMALIZED = 1 << 0,
		AS_INT = 1 << 1,
		INSTANCED = 1 << 2
	};
	u8 components_count;
	u8 byte_offset;
	AttributeType type;
	u8 flags;
};
#pragma pack()

struct LUMIX_RENDERER_API VertexDecl {
	enum { MAX_ATTRIBUTES = 16 };

	VertexDecl(PrimitiveType pt);
	void addAttribute(u8 byte_offset, u8 components_num, AttributeType type, u8 flags);
	u32 getStride() const;
	void computeHash();
	void setPrimitiveType(PrimitiveType type);

	u8 attributes_count = 0;
	u32 hash;
	Attribute attributes[MAX_ATTRIBUTES];
	PrimitiveType primitive_type = PrimitiveType::NONE;
};


struct TextureDesc {
	TextureFormat format;
	u32 width;
	u32 height;
	u32 depth;
	u32 mips;
	bool is_cubemap;
};


struct MemoryStats {
	u64 total_available_mem;
	u64 current_available_mem;
	u64 dedicated_vidmem;
	u64 render_target_mem;
	u64 buffer_mem;
	u64 texture_mem;
};

// Most common combination of arguments, can be drawn with single function call
// useful especially in debug build
struct Drawcall {
	gpu::ProgramHandle program;
	gpu::BufferHandle index_buffer;
	gpu::BufferHandle vertex_buffers[2];
	u32 vertex_buffer_offsets[2];
	u32 vertex_buffer_sizes[2];
	gpu::BufferHandle uniform_buffer2;
	u32 uniform_buffer2_offset;
	u32 uniform_buffer2_size;
	u32 indices_count;
	u32 instances_count;
	DataType index_type;
};

void preinit(IAllocator& allocator, bool load_renderdoc);
IAllocator& getAllocator();
bool init(void* window_handle, InitFlags flags);
void captureFrame();
bool getMemoryStats(MemoryStats& stats);
u32 present();
void enableVSync(bool enable);
bool isVSyncEnabled();
void waitFrame(u32 frame);
bool frameFinished(u32 frame);
LUMIX_RENDERER_API bool isOriginBottomLeft();
void checkThread();
void shutdown();
int getSize(AttributeType type);
u32 getSize(TextureFormat format, u32 w, u32 h);
u32 getBytesPerPixel(TextureFormat format);

// using profiler::pushCounter to push some gpu stats (clocks, memory, etc)
// the stats might not be available on all platforms
void pushGPUCounters();

TextureHandle allocTextureHandle();
BufferHandle allocBufferHandle();
ProgramHandle allocProgramHandle();

QueryHandle createQuery(QueryType type);

void createProgram(ProgramHandle prog, StateFlags state, const VertexDecl& decl, const char* src, ShaderType type, const char* name);
void createBuffer(BufferHandle handle, BufferFlags flags, size_t size, const void* data, const char* debug_name);
void createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, TextureFlags flags, const char* debug_name);
void createTextureView(TextureHandle view, TextureHandle texture, u32 layer, u32 mip);

void memoryBarrier(BufferHandle buffer);
void memoryBarrier(TextureHandle texture);
void barrier(TextureHandle texture, BarrierType type);
void barrier(BufferHandle buffer, BarrierType type);

void destroy(TextureHandle texture);
void destroy(BufferHandle buffer);
void destroy(ProgramHandle program);
void destroy(QueryHandle query);
	
void setCurrentWindow(void* window_handle);
void setFramebuffer(const TextureHandle* attachments, u32 num, TextureHandle ds, FramebufferFlags flags);
void setFramebufferCube(TextureHandle cube, u32 face, u32 mip);
void viewport(u32 x, u32 y, u32 w, u32 h);
void scissor(u32 x,u32 y,u32 w,u32 h);
void clear(ClearFlags flags, const float* color, float depth);
	
void useProgram(ProgramHandle program);

// call this on render thread, the call getDisassembly periodically to get the result (it returns true)
void requestDisassembly(ProgramHandle program);

// safe to call from any thread
// if disassembly is not available, returns false
// otherwise returns true and writes disassembly to the `output`
bool getDisassembly(ProgramHandle program, struct String& output);

BindlessHandle getBindlessHandle(BufferHandle buffer); // safe to call from any job
BindlessHandle getBindlessHandle(TextureHandle texture); // safe to call from any job
RWBindlessHandle getRWBindlessHandle(BufferHandle buffer); // safe to call from any job
RWBindlessHandle getRWBindlessHandle(TextureHandle texture); // safe to call from any job
void bindIndexBuffer(BufferHandle buffer);
void bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride);
void bindUniformBuffer(u32 ub_index, BufferHandle buffer, size_t offset, size_t size);
void bindIndirectBuffer(BufferHandle buffer);
void bindShaderBuffers(Span<BufferHandle> buffers);

void drawArrays(u32 offset, u32 count);
void drawIndirect(DataType index_type, u32 indirect_buffer_offset);
void drawIndexed(u32 offset, u32 count, DataType type);
void drawArraysInstanced(u32 indices_count, u32 instances_count);
void drawIndexedInstanced(u32 indices_count, u32 instances_count, DataType index_type);
void draw(const Drawcall& draw);
void dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z);

void copy(TextureHandle dst, TextureHandle src, u32 dst_x, u32 dst_y);
void copy(BufferHandle dst, BufferHandle src, u32 dst_offset, u32 src_offset, u32 size);
void copy(BufferHandle dst, TextureHandle src);
	
using TextureReadCallback = Delegate<void(Span<const u8>)>;
void readTexture(TextureHandle texture, TextureReadCallback callback);
void setDebugName(TextureHandle texture, const char* debug_name);
	
void update(TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, TextureFormat format, const void* buf, u32 size);
void update(BufferHandle buffer, const void* data, size_t size);
	
void* map(BufferHandle buffer, size_t size);
void unmap(BufferHandle buffer);
void queryTimestamp(QueryHandle query);

void beginQuery(QueryHandle query);
void endQuery(QueryHandle query);
u64 getQueryResult(QueryHandle query);
u64 getQueryFrequency();
bool isQueryReady(QueryHandle query);

void pushDebugGroup(const char* msg);
void popDebugGroup();

inline StateFlags getBlendStateBits(BlendFactors src_rgb, BlendFactors dst_rgb, BlendFactors src_a, BlendFactors dst_a)
{
	return StateFlags((((u64)src_rgb & 15) << 7) | (((u64)dst_rgb & 15) << 11) | (((u64)src_a & 15) << 15) | (((u64)dst_a & 15) << 19));
}

inline StateFlags getStencilStateBits(u8 write_mask, StencilFuncs func, u8 ref, u8 mask, StencilOps sfail, StencilOps dpfail, StencilOps dppass)
{
	return StateFlags(((u64)write_mask << 23) | ((u64)func << 31) | ((u64)ref << 35) | ((u64)mask << 43) | ((u64)sfail << 51) | ((u64)dpfail << 55) | ((u64)dppass << 59));
}

} // namespace gpu

} // namespace Lumix