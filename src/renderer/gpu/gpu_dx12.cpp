//https://microsoft.github.io/DirectX-Specs/

#include "core/allocator.h"
#include "core/array.h"
#include "core/atomic.h"
#include "core/hash_map.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "core/string.h"
#include "core/sync.h"
#include "core/tag_allocator.h"
#include "renderer/gpu/gpu.h"
#include <Windows.h>
#include <cassert>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#ifdef LUMIX_DEBUG
	#include <dxgidebug.h>
#endif
#include <dxgi1_6.h>
#include <malloc.h>

#include "renderer/gpu/renderdoc_app.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef LUMIX_DEBUG
	#define USE_PIX
	#pragma comment(lib, "WinPixEventRuntime.lib")
	#include "pix3.h"
#endif

namespace Lumix::gpu {

static constexpr u32 NUM_BACKBUFFERS = 2;
static constexpr u32 SCRATCH_BUFFER_SIZE = 8 * 1024 * 1024;
static constexpr u32 MAX_SRV_DESCRIPTORS = 16 * 1024;
static constexpr u32 TIMESTAMP_QUERY_COUNT = 2048;
static constexpr u32 STATS_QUERY_COUNT = 128;
static constexpr u32 INVALID_HEAP_ID = 0xffFFffFF;
static constexpr u32 BINDLESS_SRV_ROOT_PARAMETER_INDEX = 6;
static constexpr u32 BINDLESS_SAMPLERS_ROOT_PARAMETER_INDEX = 7;
static constexpr u32 SRV_ROOT_PARAMETER_INDEX = 8;

static DXGI_FORMAT getDXGIFormat(const Attribute& attr) {
	const bool as_int = attr.flags & Attribute::AS_INT;
	switch (attr.type) {
		case AttributeType::FLOAT:
			switch (attr.components_count) {
				case 1: return DXGI_FORMAT_R32_FLOAT;
				case 2: return DXGI_FORMAT_R32G32_FLOAT;
				case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
				case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
			break;

		case AttributeType::I8: 
			switch(attr.components_count) {
				case 1: return as_int ? DXGI_FORMAT_R8_SINT : DXGI_FORMAT_R8_SNORM;
				case 2: return as_int ? DXGI_FORMAT_R8G8_SINT : DXGI_FORMAT_R8G8_SNORM;
				case 4: return as_int ? DXGI_FORMAT_R8G8B8A8_SINT : DXGI_FORMAT_R8G8B8A8_SNORM;
			}
			break;
		case AttributeType::U8: 
			switch(attr.components_count) {
				case 1: return as_int ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_R8_UNORM;
				case 2: return as_int ? DXGI_FORMAT_R8G8_UINT : DXGI_FORMAT_R8G8_UNORM;
				case 4: return as_int ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
			}
			break;
		case AttributeType::I16: 
			switch(attr.components_count) {
				case 4: return as_int ? DXGI_FORMAT_R16G16B16A16_SINT : DXGI_FORMAT_R16G16B16A16_SNORM;
			}
			break;
	}
	ASSERT(false);
	return DXGI_FORMAT_R32_FLOAT;
}

static u32 sizeDXTC(u32 w, u32 h, DXGI_FORMAT format) {
	const bool is_dxt1 = format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC1_UNORM_SRGB;
	const bool is_ati = format == DXGI_FORMAT_BC4_UNORM;
	return ((w + 3) / 4) * ((h + 3) / 4) * (is_dxt1 || is_ati ? 8 : 16);
}

struct FormatDesc {
	bool compressed;
	u32 block_bytes;
	DXGI_FORMAT internal;
	DXGI_FORMAT internal_srgb;

	u32 getRowPitch(u32 w) const {
		if (compressed) {
			return (w + 3) / 4 * block_bytes;
		}

		return w * block_bytes;
	}
	
	static FormatDesc get(DXGI_FORMAT format) {
		switch(format) {
			case DXGI_FORMAT_BC1_UNORM : return get(TextureFormat::BC1);
			case DXGI_FORMAT_BC2_UNORM : return get(TextureFormat::BC2);
			case DXGI_FORMAT_BC3_UNORM : return get(TextureFormat::BC3);
			case DXGI_FORMAT_BC4_UNORM : return get(TextureFormat::BC4);
			case DXGI_FORMAT_BC5_UNORM : return get(TextureFormat::BC5);
			case DXGI_FORMAT_R16_UNORM : return get(TextureFormat::R16);
			case DXGI_FORMAT_R8_UNORM : return get(TextureFormat::R8);
			case DXGI_FORMAT_R8G8_UNORM : return get(TextureFormat::RG8);
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : return get(TextureFormat::SRGBA);
			case DXGI_FORMAT_R8G8B8A8_UNORM : return get(TextureFormat::RGBA8);
			case DXGI_FORMAT_R16G16B16A16_UNORM : return get(TextureFormat::RGBA16);
			case DXGI_FORMAT_R16G16B16A16_FLOAT : return get(TextureFormat::RGBA16F);
			case DXGI_FORMAT_R32G32B32A32_FLOAT : return get(TextureFormat::RGBA32F);
			case DXGI_FORMAT_R11G11B10_FLOAT : return get(TextureFormat::R11G11B10F);
			case DXGI_FORMAT_R32G32_FLOAT : return get(TextureFormat::RG32F);
			case DXGI_FORMAT_R32G32B32_FLOAT : return get(TextureFormat::RGB32F);
			case DXGI_FORMAT_R16G16_FLOAT : return get(TextureFormat::RG16F);
			
			case DXGI_FORMAT_R32_TYPELESS : return get(TextureFormat::D32);
			case DXGI_FORMAT_R24G8_TYPELESS : return get(TextureFormat::D24S8);
			default: ASSERT(false); return {}; 
		}
	}

	static FormatDesc get(TextureFormat format) {
		switch(format) {
			case TextureFormat::BC1: return {			true,		8,	DXGI_FORMAT_BC1_UNORM,				DXGI_FORMAT_BC1_UNORM_SRGB};
			case TextureFormat::BC2: return {			true,		16,	DXGI_FORMAT_BC2_UNORM,				DXGI_FORMAT_BC2_UNORM_SRGB};
			case TextureFormat::BC3: return {			true,		16,	DXGI_FORMAT_BC3_UNORM,				DXGI_FORMAT_BC3_UNORM_SRGB};
			case TextureFormat::BC4: return {			true,		8,	DXGI_FORMAT_BC4_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::BC5: return {			true,		16,	DXGI_FORMAT_BC5_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R16: return {			false,		2,	DXGI_FORMAT_R16_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG16: return {			false,		4,	DXGI_FORMAT_R16G16_UNORM,			DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R8: return {			false,		1,	DXGI_FORMAT_R8_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG8: return {			false,		2,	DXGI_FORMAT_R8G8_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::BGRA8: return {			false,		4,	DXGI_FORMAT_B8G8R8A8_UNORM,			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
			case TextureFormat::SRGBA: return {			false,		4,	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
			case TextureFormat::RGBA8: return {			false,		4,	DXGI_FORMAT_R8G8B8A8_UNORM,			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
			case TextureFormat::RGBA16: return {		false,		8,	DXGI_FORMAT_R16G16B16A16_UNORM,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R11G11B10F: return {	false,		4,	DXGI_FORMAT_R11G11B10_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RGBA16F: return {		false,		8,	DXGI_FORMAT_R16G16B16A16_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RGBA32F: return {		false,		16, DXGI_FORMAT_R32G32B32A32_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG32F: return {			false,		8,	DXGI_FORMAT_R32G32_FLOAT,			DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RGB32F: return {		false,		12,	DXGI_FORMAT_R32G32B32_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R32F: return {			false,		4,	DXGI_FORMAT_R32_FLOAT,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG16F: return {			false,		4,	DXGI_FORMAT_R16G16_FLOAT,			DXGI_FORMAT_UNKNOWN};

			case TextureFormat::D32: return {			false,		4,	DXGI_FORMAT_R32_TYPELESS,			DXGI_FORMAT_UNKNOWN};
			case TextureFormat::D24S8: return {			false,		4,	DXGI_FORMAT_R24G8_TYPELESS,			DXGI_FORMAT_UNKNOWN};
			default: ASSERT(false); return {}; 
		}
	}
};

u32 getSize(TextureFormat format, u32 w, u32 h) {
	const FormatDesc& desc = FormatDesc::get(format);
	if (desc.compressed) return sizeDXTC(w, h, desc.internal);
	return desc.block_bytes * w * h;
}

static u32 getSize(DXGI_FORMAT format) {
	switch(format) {
		case DXGI_FORMAT_R8_UNORM: return 1;
		case DXGI_FORMAT_R8G8_UNORM: return 2;
		case DXGI_FORMAT_R32_TYPELESS: return 4;
		case DXGI_FORMAT_R24G8_TYPELESS: return 4;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4;
		case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 4;
		case DXGI_FORMAT_B8G8R8A8_UNORM: return 4;
		case DXGI_FORMAT_R16G16B16A16_UNORM: return 8;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
		case DXGI_FORMAT_R32G32_FLOAT: return 8;
		case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
		case DXGI_FORMAT_R16_UNORM: return 2;
		case DXGI_FORMAT_R16_FLOAT: return 2;
		case DXGI_FORMAT_R32_FLOAT: return 4;
		default: ASSERT(false); return 0;
	}
}

static DXGI_FORMAT getDXGIFormat(TextureFormat format, bool is_srgb) {
	const FormatDesc& fd = FormatDesc::get(format);
	return is_srgb && fd.internal_srgb != DXGI_FORMAT_UNKNOWN ? fd.internal_srgb : fd.internal;
}


template <int N> static void toWChar(WCHAR (&out)[N], const char* in) {
	const char* c = in;
	WCHAR* cout = out;
	while (*c && c - in < N - 1) {
		*cout = *c;
		++cout;
		++c;
	}
	*cout = 0;
}

static bool isDepthFormat(DXGI_FORMAT format) {
	switch (format) {
		case DXGI_FORMAT_R24G8_TYPELESS: return true;
		case DXGI_FORMAT_R32_TYPELESS: return true;
		default: return false;
	}
}

static DXGI_FORMAT toViewFormat(DXGI_FORMAT format) {
	switch (format) {
		case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
		default: return format;
	}
}

static DXGI_FORMAT toDSViewFormat(DXGI_FORMAT format) {
	switch (format) {
		case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
		default: return format;
	}
}

static u32 calcSubresource(u32 mip, u32 array, u32 mip_count) {
	return mip + array * mip_count;
}

static void switchState(ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES old_state, D3D12_RESOURCE_STATES new_state) {
	D3D12_RESOURCE_BARRIER barrier;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = old_state;
	barrier.Transition.StateAfter = new_state;
	cmd_list->ResourceBarrier(1, &barrier);
}

struct Query {
	u64 result = 0;
	u32 idx;
	QueryType type;
	bool ready;
};

struct Program {
	Program(IAllocator& allocator) : disassembly(allocator) {}

	ID3DBlob* vs = nullptr;
	ID3DBlob* ps = nullptr;
	ID3DBlob* cs = nullptr;
	D3D12_INPUT_ELEMENT_DESC attributes[16];
	u32 attribute_count = 0;
	StateFlags state;
	D3D12_PRIMITIVE_TOPOLOGY primitive_topology;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
	// for CS, there's 1:1 mapping from `shader_hash` to PSO
	// for VS/PS, there's 1:1 mapping from `shader_hash` and RT formats to PSO
	StableHash shader_hash;
	String disassembly;
	#ifdef LUMIX_DEBUG
		StaticString<64> name;
	#endif
};

struct Buffer {
	D3D12_RESOURCE_STATES setState(ID3D12GraphicsCommandList* cmd_list, D3D12_RESOURCE_STATES new_state) {
		if (state == new_state) return state;
		D3D12_RESOURCE_STATES old_state = state;
		switchState(cmd_list, resource, state, new_state);
		state = new_state;
		return old_state;
	}

	ID3D12Resource* resource = nullptr;
	u8* mapped_ptr = nullptr;
	u32 size = 0;
	D3D12_RESOURCE_STATES state;
	u32 heap_id = INVALID_HEAP_ID;
	#ifdef LUMIX_DEBUG
		StaticString<64> name;
	#endif
};

struct Texture {
	D3D12_RESOURCE_STATES setState(ID3D12GraphicsCommandList* cmd_list, D3D12_RESOURCE_STATES new_state) {
		if (state == new_state) return state;
		D3D12_RESOURCE_STATES old_state = state;
		switchState(cmd_list, resource, state, new_state);
		state = new_state;
		return old_state;
	}

	ID3D12Resource* resource;
	D3D12_RESOURCE_STATES state;
	u32 heap_id = INVALID_HEAP_ID;
	DXGI_FORMAT dxgi_format;
	TextureFlags flags;
	u32 w;
	u32 h;
	bool is_view = false;
	#ifdef LUMIX_DEBUG
		StaticString<64> name;
	#endif
};

struct FrameBuffer {
	D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil = {};
	D3D12_CPU_DESCRIPTOR_HANDLE render_targets[8] = {};
	DXGI_FORMAT formats[8] = {};
	DXGI_FORMAT ds_format = {};
	TextureHandle attachments[9] = {};
	u32 count = 0;
};

struct ShaderCompiler {
	ShaderCompiler(IAllocator& allocator)
		: m_allocator(allocator, "shader compiler")
		, m_cache(m_allocator) {}

	bool compile(const VertexDecl& decl
		, const char* src
		, ShaderType type
		, const char* name
		, Program& program)
	{
		program.attribute_count = decl.attributes_count;
		for (u8 i = 0; i < decl.attributes_count; ++i) {
			const Attribute& attr = decl.attributes[i];
			const bool instanced = attr.flags & Attribute::INSTANCED;
			program.attributes[i].AlignedByteOffset = attr.byte_offset;
			program.attributes[i].Format = getDXGIFormat(attr);
			program.attributes[i].SemanticIndex = i;
			program.attributes[i].SemanticName = "TEXCOORD";
			program.attributes[i].InputSlot = instanced ? 1 : 0;
			program.attributes[i].InputSlotClass = instanced ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			program.attributes[i].InstanceDataStepRate = instanced ? 1 : 0;
		}

		RollingStableHasher hasher;
		hasher.begin();
		hasher.update(src, stringLength(src));
		hasher.update(&program.primitive_topology, sizeof(program.primitive_topology));
		const StableHash hash = hasher.end64();
		program.shader_hash = hash;
		if (type == ShaderType::SURFACE) {
			// TODO surface shader cache
			program.vs = compileStage(hash, src, "vs_5_1", name, "mainVS");
			if (!program.vs) return false;

			program.ps = compileStage(hash, src, "ps_5_1", name, "mainPS");
			if (!program.ps) return false;
		}
		else {
			ASSERT(type == ShaderType::COMPUTE);
			auto iter = m_cache.find(hash);
			if (iter.isValid()) {
				program.cs = iter.value();
				return true;
			}

			program.cs = compileStage(hash, src, "cs_5_1", name, "main");
			if (!program.cs) return false;
			if (program.cs->GetBufferSize() == 0) {
				program.cs->Release();
				program.cs = nullptr;
				return false;
			}
		}

		return true;
	}

	ID3DBlob* compileStage(StableHash hash, const char* src, const char* target, const char* name, const char* entry_point) {
		ID3DBlob* output = NULL;
		ID3DBlob* errors = NULL;
		HRESULT hr = D3DCompile(src,
			strlen(src) + 1,
			name,
			NULL,
			NULL,
			entry_point,
			target,
			D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
			0,
			&output,
			&errors);
		if (errors) {
			if (SUCCEEDED(hr)) {
				logInfo("gpu: ", (LPCSTR)errors->GetBufferPointer());
			} else {
				logError((LPCSTR)errors->GetBufferPointer());
			}
			errors->Release();
			if (FAILED(hr)) return nullptr;
		}
		ASSERT(output);
		if (output->GetBufferSize() == 0) {
			output->Release();
			return nullptr;
		}
		m_cache.insert(hash, output);

		// save disassembled files
		#if 0
			ID3DBlob* disassembly;
			HRESULT res = D3DDisassemble(output->GetBufferPointer(), output->GetBufferSize(), 0, NULL, &disassembly);
			ASSERT(res == S_OK);
			const char* dism = (const char*)disassembly->GetBufferPointer();
			//logInfo(name, ": ", StringView(dism, (u32)disassembly->GetBufferSize()));

			os::OutputFile out;
			if (out.open(StaticString<MAX_PATH>("hlsl_", Path::getBasename(name), "-", entry_point, ".asm"))) {
				(void)out.write(dism, disassembly->GetBufferSize());
				out.close();
			}
		#endif
		return output;
	};

	void saveCache(const char* filename) {
		os::OutputFile file;
		if (file.open(filename)) {
			u32 version = 0;
			bool success = file.write(&version, sizeof(version));
			for (auto iter = m_cache.begin(), end = m_cache.end(); iter != end; ++iter) {
				const StableHash hash = iter.key();
				ID3DBlob* blob = iter.value();
				const u32 size = (u32)blob->GetBufferSize();
				success = file.write(&hash, sizeof(hash)) && success;
				success = file.write(&size, sizeof(size)) && success;
				success = file.write(blob->GetBufferPointer(), size) && success;
			}
			if (!success) {
				logError("Could not write ", filename);
			}
			file.close();
		}
	}

	void loadCache(const char* filename) {
		PROFILE_FUNCTION();
		os::InputFile file;
		if (file.open(filename)) {
			u32 version;
			if (!file.read(&version, sizeof(version))) {
				logError("Could not read ", filename);
			}
			ASSERT(version == 0);
			StableHash hash;
			while (file.read(&hash, sizeof(hash))) {
				u32 size;
				if (file.read(&size, sizeof(size))) {
					ID3DBlob* blob;
					HRESULT res = D3DCreateBlob(size, &blob);
					if (FAILED(res)) {
						logError("Failed to create blob");
						break;
					}
					if (!file.read(blob->GetBufferPointer(), size)) break;
					m_cache.insert(hash, blob);
				} else {
					break;
				}
			}
			file.close();
		}
	}

	static const char* getTypeDefine(gpu::ShaderType type) {
		return "";
	}

	TagAllocator m_allocator;
	
	// cache source code -> binary blob
	HashMap<StableHash, ID3DBlob*> m_cache;
};

struct PSOCache {
	PSOCache(IAllocator& allocator)
		: cache(allocator)
	{}

	ID3D12PipelineState* getPipelineStateCompute(ID3D12Device* device, ID3D12RootSignature* root_signature, ProgramHandle program) {
		auto iter = cache.find(program->shader_hash);
		if (iter.isValid()) return iter.value();

		if (!program->cs) return nullptr;

		Program& p = *program;
		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.CS = {p.cs->GetBufferPointer(), p.cs->GetBufferSize()};
		desc.NodeMask = 1;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		desc.pRootSignature = root_signature;

		ID3D12PipelineState* pso;
		HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
		ASSERT(hr == S_OK);
		cache.insert(program->shader_hash, pso);
		return pso;
	}

	ID3D12PipelineState* getPipelineState(ID3D12Device* device
		, ProgramHandle program
		, const FrameBuffer& fb
		, ID3D12RootSignature* root_signature)
	{
		ASSERT(program);

		Program& p = *program;
		RollingStableHasher hasher;
		hasher.begin();
		hasher.update(&p.shader_hash, sizeof(p.shader_hash));
		hasher.update(&fb.ds_format, sizeof(fb.ds_format));
		hasher.update(&fb.formats[0], sizeof(fb.formats[0]) * fb.count);
		const StableHash hash = hasher.end64();

		auto iter = cache.find(hash);
		if (iter.isValid()) {
			last = iter.value();
			return iter.value();
		}

		if (!program->vs && !program->ps && !program->cs) return nullptr;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		if (p.vs) desc.VS = {p.vs->GetBufferPointer(), p.vs->GetBufferSize()};
		if (p.ps) desc.PS = {p.ps->GetBufferPointer(), p.ps->GetBufferSize()};

		desc.PrimitiveTopologyType = program->primitive_topology_type;

		const StateFlags state = program->state;
		if (u64(state & StateFlags::CULL_BACK)) {
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		} else if (u64(state & StateFlags::CULL_FRONT)) {
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		} else {
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		}

		desc.pRootSignature = root_signature;
		desc.RasterizerState.FrontCounterClockwise = TRUE;
		desc.RasterizerState.FillMode = u64(state & StateFlags::WIREFRAME) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
		// TODO enable/disable scissor
		desc.RasterizerState.DepthClipEnable = FALSE;

		desc.DepthStencilState.DepthEnable = u64(state & StateFlags::DEPTH_FUNCTION) != 0;
		desc.DepthStencilState.DepthWriteMask = u64(state & StateFlags::DEPTH_WRITE) && desc.DepthStencilState.DepthEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		if (u64(state & StateFlags::DEPTH_FN_GREATER)) {
			desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
		}
		else if (u64(state & StateFlags::DEPTH_FN_EQUAL)) {
			desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
		}
		else {
			desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		}

		const StencilFuncs func = (StencilFuncs)((u64(state) >> 31) & 0xf);
		desc.DepthStencilState.StencilEnable = func != StencilFuncs::DISABLE;
		if (desc.DepthStencilState.StencilEnable) {
			desc.DepthStencilState.StencilReadMask = u8(u64(state) >> 43);
			desc.DepthStencilState.StencilWriteMask = u8(u64(state) >> 23);
			D3D12_COMPARISON_FUNC dx_func;
			switch (func) {
				case StencilFuncs::ALWAYS: dx_func = D3D12_COMPARISON_FUNC_ALWAYS; break;
				case StencilFuncs::EQUAL: dx_func = D3D12_COMPARISON_FUNC_EQUAL; break;
				case StencilFuncs::NOT_EQUAL: dx_func = D3D12_COMPARISON_FUNC_NOT_EQUAL; break;
				default: ASSERT(false); break;
			}
			auto toDXOp = [](StencilOps op) {
				constexpr D3D12_STENCIL_OP table[] = {D3D12_STENCIL_OP_KEEP,
					D3D12_STENCIL_OP_ZERO,
					D3D12_STENCIL_OP_REPLACE,
					D3D12_STENCIL_OP_INCR_SAT,
					D3D12_STENCIL_OP_DECR_SAT,
					D3D12_STENCIL_OP_INVERT,
					D3D12_STENCIL_OP_INCR,
					D3D12_STENCIL_OP_DECR};
				return table[(int)op];
			};
			const D3D12_STENCIL_OP sfail = toDXOp(StencilOps((u64(state) >> 51) & 0xf));
			const D3D12_STENCIL_OP zfail = toDXOp(StencilOps((u64(state) >> 55) & 0xf));
			const D3D12_STENCIL_OP zpass = toDXOp(StencilOps((u64(state) >> 59) & 0xf));

			desc.DepthStencilState.FrontFace.StencilFailOp = sfail;
			desc.DepthStencilState.FrontFace.StencilDepthFailOp = zfail;
			desc.DepthStencilState.FrontFace.StencilPassOp = zpass;
			desc.DepthStencilState.FrontFace.StencilFunc = dx_func;

			desc.DepthStencilState.BackFace.StencilFailOp = sfail;
			desc.DepthStencilState.BackFace.StencilDepthFailOp = zfail;
			desc.DepthStencilState.BackFace.StencilPassOp = zpass;
			desc.DepthStencilState.BackFace.StencilFunc = dx_func;
		}

		const u16 blend_bits = u16(u64(state) >> 7);

		auto to_dx = [&](BlendFactors factor) -> D3D12_BLEND {
			static const D3D12_BLEND table[] = {
				D3D12_BLEND_ZERO,
				D3D12_BLEND_ONE,
				D3D12_BLEND_SRC_COLOR,
				D3D12_BLEND_INV_SRC_COLOR,
				D3D12_BLEND_SRC_ALPHA,
				D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_DEST_COLOR,
				D3D12_BLEND_INV_DEST_COLOR,
				D3D12_BLEND_DEST_ALPHA,
				D3D12_BLEND_INV_DEST_ALPHA,
				D3D12_BLEND_SRC1_COLOR,
				D3D12_BLEND_INV_SRC1_COLOR,
				D3D12_BLEND_SRC1_ALPHA,
				D3D12_BLEND_INV_SRC1_ALPHA,
			};
			ASSERT((u32)factor < lengthOf(table));
			return table[(int)factor];
		};

		for (u32 rt_idx = 0; rt_idx < (u32)lengthOf(desc.BlendState.RenderTarget); ++rt_idx) {
			if (blend_bits) {
				const BlendFactors src_rgb = (BlendFactors)(blend_bits & 0xf);
				const BlendFactors dst_rgb = (BlendFactors)((blend_bits >> 4) & 0xf);
				const BlendFactors src_a = (BlendFactors)((blend_bits >> 8) & 0xf);
				const BlendFactors dst_a = (BlendFactors)((blend_bits >> 12) & 0xf);

				desc.BlendState.RenderTarget[rt_idx].BlendEnable = true;
				desc.BlendState.AlphaToCoverageEnable = false;
				desc.BlendState.RenderTarget[rt_idx].SrcBlend = to_dx(src_rgb);
				desc.BlendState.RenderTarget[rt_idx].DestBlend = to_dx(dst_rgb);
				desc.BlendState.RenderTarget[rt_idx].BlendOp = D3D12_BLEND_OP_ADD;
				desc.BlendState.RenderTarget[rt_idx].SrcBlendAlpha = to_dx(src_a);
				desc.BlendState.RenderTarget[rt_idx].DestBlendAlpha = to_dx(dst_a);
				desc.BlendState.RenderTarget[rt_idx].BlendOpAlpha = D3D12_BLEND_OP_ADD;
				desc.BlendState.RenderTarget[rt_idx].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			} else {
				desc.BlendState.RenderTarget[rt_idx].BlendEnable = false;
				desc.BlendState.RenderTarget[rt_idx].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				desc.BlendState.RenderTarget[rt_idx].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				desc.BlendState.RenderTarget[rt_idx].BlendOp = D3D12_BLEND_OP_ADD;
				desc.BlendState.RenderTarget[rt_idx].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
				desc.BlendState.RenderTarget[rt_idx].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
				desc.BlendState.RenderTarget[rt_idx].BlendOpAlpha = D3D12_BLEND_OP_ADD;
				desc.BlendState.RenderTarget[rt_idx].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			}
		}

		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		desc.NodeMask = 1;
		desc.SampleMask = 0xffFFffFF;

		desc.InputLayout.NumElements = p.attribute_count;
		desc.InputLayout.pInputElementDescs = p.attributes;

		desc.DSVFormat = fb.ds_format;
		desc.NumRenderTargets = fb.count;
		for (u32 i = 0; i < fb.count; ++i) {
			desc.RTVFormats[i] = fb.formats[i];
		}

		ID3D12PipelineState* pso;
		HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
		ASSERT(hr == S_OK);
		cache.insert(hash, pso);
		last = pso;
		return pso;
	}

	// TODO separate compute and graphics cache
	// TODO graphics cache should be [framebuffer][shader_hash] -> PSO, and [framebuffer] can be computed once in setFramebuffer
	HashMap<StableHash, ID3D12PipelineState*> cache;
	ID3D12PipelineState* last = nullptr;
};

// TODO actually use gpu::TextureHandle.flags 
struct SamplerHeap {
	enum class SamplerFlags {
		NONE = 0,
		ANISOTROPIC_FILTER = 1 << 0,
		CLAMP_U = 1 << 1,
		CLAMP_V = 1 << 2,
		CLAMP_W = 1 << 3,
		POINT_FILTER = 1 << 4
	};

	void alloc(ID3D12Device* device, u32 id, SamplerFlags flags) {
		D3D12_SAMPLER_DESC desc = {};
		const bool is_aniso = u32(flags & SamplerFlags::ANISOTROPIC_FILTER);
		desc.AddressU = u32(flags & SamplerFlags::CLAMP_U) != 0 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressV = u32(flags & SamplerFlags::CLAMP_V) != 0 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressW = u32(flags & SamplerFlags::CLAMP_W) != 0 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MipLODBias = 0;
		desc.Filter = is_aniso ? D3D12_FILTER_ANISOTROPIC : u32(flags & SamplerFlags::POINT_FILTER) ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.MaxLOD = 1000;
		desc.MinLOD = -1000;
		desc.MaxAnisotropy = is_aniso ? 8 : 1;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_begin;
		cpu.ptr += increment * id;
		device->CreateSampler(&desc, cpu);
	}

	bool init(ID3D12Device* device, u32 num_descriptors) {
		D3D12_DESCRIPTOR_HEAP_DESC desc;
		desc.NumDescriptors = num_descriptors;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = 1;
		if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)) != S_OK) return false;

		increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		gpu_begin = heap->GetGPUDescriptorHandleForHeapStart();
		cpu_begin = heap->GetCPUDescriptorHandleForHeapStart();
		max_count = num_descriptors;

		alloc(device, 0, SamplerFlags::CLAMP_U | SamplerFlags::CLAMP_V | SamplerFlags::CLAMP_W);
		alloc(device, 1, SamplerFlags::NONE);

		return true;
	}

	ID3D12DescriptorHeap* heap = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_begin;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_begin;
	u32 increment = 0;
	u32 count = 0;
	u32 max_count = 0;
};

struct SRVUAVHeap {
	SRVUAVHeap(IAllocator& allocator)
		: free_list(allocator) {}

	void free(u32 id) {
		free_list.push(id);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE allocTransient(ID3D12Device* device, Span<ID3D12Resource*> resources, Span<const D3D12_SHADER_RESOURCE_VIEW_DESC> srv_descs) {
		ASSERT(resources.length() == srv_descs.length());
		ASSERT(transient_count + resources.length() <= max_transient_count);
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
		gpu.ptr += (frame * max_transient_count + transient_count) * handle_increment_size;
		cpu.ptr += (frame * max_transient_count + transient_count) * handle_increment_size;
		for (u32 i = 0; i < resources.length(); ++i) {
			if (resources[i]) device->CreateShaderResourceView(resources[i], &srv_descs[i], cpu);
			cpu.ptr += handle_increment_size;
		}
		transient_count += resources.length();
		return gpu;
	}

	void alloc(ID3D12Device* device, u32 heap_id, ID3D12Resource* res, const D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc) {
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
		cpu.ptr += heap_id * handle_increment_size;
		device->CreateShaderResourceView(res, &srv_desc, cpu);
		if (uav_desc) {
			cpu.ptr += handle_increment_size;
			device->CreateUnorderedAccessView(res, nullptr, uav_desc, cpu);
		}
	}

	u32 reserveID() {
		// TODO mutex?
		//jobs::MutexGuard guard(mutex);
		ASSERT(!free_list.empty());
		u32 id = free_list.back();
		free_list.pop();
		return id;
	}

	void preinit(u32 num_resources, u32 num_transient) {
		max_transient_count = num_transient;
		max_resource_count = num_resources;
		free_list.reserve(max_resource_count);
		for (u32 i = 2; i < max_resource_count; ++i) {
			free_list.push(i * 2 + max_transient_count * NUM_BACKBUFFERS);
		}
	}

	bool init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type) {
		D3D12_DESCRIPTOR_HEAP_DESC desc;
		desc.NumDescriptors = max_resource_count * 2 + max_transient_count * NUM_BACKBUFFERS;
		desc.Type = type;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = 1;
		if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)) != S_OK) return false;

		handle_increment_size = device->GetDescriptorHandleIncrementSize(type);
		gpu_begin = heap->GetGPUDescriptorHandleForHeapStart();
		cpu_begin = heap->GetCPUDescriptorHandleForHeapStart();

		// null texture srv
		D3D12_SHADER_RESOURCE_VIEW_DESC tsrv_desc = {};
		tsrv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		tsrv_desc.Format = DXGI_FORMAT_R8G8_B8G8_UNORM;
		tsrv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		device->CreateShaderResourceView(nullptr, &tsrv_desc, cpu_begin);

		// null buffer srv
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_begin;
		cpu.ptr += handle_increment_size * 2;
		D3D12_SHADER_RESOURCE_VIEW_DESC bsrv_desc = {};
		bsrv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		bsrv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		bsrv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		device->CreateShaderResourceView(nullptr, &bsrv_desc, cpu);

		this->num_resouces = 2;

		return true;
	}

	void nextFrame() {
		transient_count = 0;
		frame = (frame + 1) % NUM_BACKBUFFERS;
	}

	Array<u32> free_list;
	ID3D12DescriptorHeap* heap = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_begin;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_begin;
	u32 handle_increment_size = 0;
	u32 num_resouces = 0;
	u32 max_resource_count = 0;
	u32 max_transient_count = 0;
	u32 transient_count = 0;
	u32 frame = 0;
	jobs::Mutex mutex;
};

struct RTVDSVHeap {
	D3D12_CPU_DESCRIPTOR_HANDLE allocDSV(ID3D12Device* device, const Texture& texture) {
		ASSERT(num_resources + 1 <= max_resource_count);

		D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_begin;
		cpu.ptr += (max_resource_count * frame + num_resources) * handle_increment_size;
		++num_resources;

		D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
		desc.Format = toDSViewFormat(texture.dxgi_format);
		desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;
		device->CreateDepthStencilView(texture.resource, &desc, cpu);
		return cpu;
		
		/*ASSERT(count + 1 <= max_count);
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu;
		gpu.ptr += count * increment;
		cpu.ptr += count * increment;
		D3D12_CPU_DESCRIPTOR_HANDLE res = cpu;
		++count;

		ASSERT(texture.resource);
		ASSERT(texture.resource);
		if (texture.resource) {
			D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
			desc.Format = toDSViewFormat(texture.dxgi_format);
			desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = 0;
			d3d->device->CreateDepthStencilView(texture.resource, &desc, cpu);
		}
		return res;*/
	}

	void nextFrame() {
		num_resources = 0;
		frame = (frame + 1) % NUM_BACKBUFFERS;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE allocRTV(ID3D12Device* device, ID3D12Resource* resource, D3D12_RENDER_TARGET_VIEW_DESC* view_desc = nullptr) {
		ASSERT(resource);
		ASSERT(num_resources + 1 <= max_resource_count);

		D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_begin;
		cpu.ptr += (max_resource_count * frame + num_resources) * handle_increment_size;
		++num_resources;

		device->CreateRenderTargetView(resource, view_desc, cpu);
		return cpu;
	}

	bool init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 num_resources) {
		const bool is_rtv = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		const bool is_dsv = type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		D3D12_DESCRIPTOR_HEAP_DESC desc;
		desc.NumDescriptors = num_resources * NUM_BACKBUFFERS;
		desc.Type = type;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)) != S_OK) return false;

		handle_increment_size = device->GetDescriptorHandleIncrementSize(type);
		cpu_begin = heap->GetCPUDescriptorHandleForHeapStart();
		max_resource_count = num_resources;

		return true;
	}

	ID3D12DescriptorHeap* heap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_begin;
	u32 handle_increment_size = 0;
	u32 max_resource_count = 0;
	u32 num_resources = 0;
	u32 frame = 0;
};

static ID3D12Resource* createBuffer(ID3D12Device* device, const void* data, u64 size, D3D12_HEAP_TYPE type) {
	D3D12_HEAP_PROPERTIES upload_heap_props;
	upload_heap_props.Type = type;
	upload_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	upload_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	upload_heap_props.CreationNodeMask = 1;
	upload_heap_props.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ID3D12Resource* upload_buffer;

	const D3D12_RESOURCE_STATES state = type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;

	HRESULT hr = device->CreateCommittedResource(&upload_heap_props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&upload_buffer));
	ASSERT(hr == S_OK);

	if (data) {
		void* ptr = nullptr;
		hr = upload_buffer->Map(0, nullptr, &ptr);
		ASSERT(hr == S_OK);

		memcpy(ptr, data, size);
		upload_buffer->Unmap(0, nullptr);
	}

	return upload_buffer;
}

struct Frame {
	struct TextureRead {
		ID3D12Resource* staging;
		TextureReadCallback callback;
		// TODO size
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[16 * 6];
		u32 num_layouts;
		u32 dst_total_bytes;
	};

	Frame(IAllocator& allocator)
		: to_release(allocator)
		, to_heap_release(allocator)
		, to_resolve(allocator)
		, to_resolve_stats(allocator)
		, texture_reads(allocator)
	{}

	void clear();

	bool init(ID3D12Device* device) {
		if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_allocator)) != S_OK) return false;
		if (device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) != S_OK) return false;

		scratch_buffer = createBuffer(device, nullptr, SCRATCH_BUFFER_SIZE, D3D12_HEAP_TYPE_UPLOAD);

		scratch_buffer->Map(0, nullptr, (void**)&scratch_buffer_begin);
		scratch_buffer_ptr = scratch_buffer_begin;

		timestamp_query_buffer = createBuffer(device, nullptr, sizeof(u64) * TIMESTAMP_QUERY_COUNT, D3D12_HEAP_TYPE_READBACK);
		stats_query_buffer = createBuffer(device, nullptr, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) * STATS_QUERY_COUNT, D3D12_HEAP_TYPE_READBACK);

		return true;
	}

	bool isFinished() {
		return fence->GetCompletedValue() == fence_value;
	}

	void wait() {
		if (fence_value != 0) {
			fence->SetEventOnCompletion(fence_value, nullptr);
		}
	}

	void begin();
	void end(ID3D12CommandQueue* cmd_queue, ID3D12GraphicsCommandList* cmd_list, ID3D12QueryHeap* timestamp_query_heap, ID3D12QueryHeap* stats_query_heap);

	ID3D12Resource* scratch_buffer = nullptr;
	u8* scratch_buffer_ptr = nullptr;
	u8* scratch_buffer_begin = nullptr;
	ID3D12CommandAllocator* cmd_allocator = nullptr;
	Array<IUnknown*> to_release;
	Array<u32> to_heap_release;
	ID3D12Fence* fence = nullptr;
	u64 fence_value = 0;
	Array<Query*> to_resolve;
	Array<Query*> to_resolve_stats;
	Array<TextureRead> texture_reads;
	ID3D12Resource* timestamp_query_buffer;
	ID3D12Resource* stats_query_buffer;
	u8* timestamp_query_buffer_ptr;
	u8* stats_query_buffer_ptr;
	bool capture_requested = false;
};

struct SRV {
	TextureHandle texture;
	BufferHandle buffer;
};

struct D3D {

	struct Window {
		void* handle = nullptr;
		IDXGISwapChain3* swapchain = nullptr;
		ID3D12Resource* backbuffers[NUM_BACKBUFFERS] = {};
		IVec2 size = IVec2(800, 600);
		u64 last_used_frame = 0;
	};

	D3D(IAllocator& allocator) 
		: allocator(allocator) 
		, srv_heap(allocator)
		, shader_compiler(allocator)
		, frames(allocator)
		, pso_cache(allocator)
	{}

	IAllocator& allocator;
	DWORD thread;
	RENDERDOC_API_1_0_2* rdoc_api = nullptr;
	ID3D12Device* device = nullptr;
	ID3D12RootSignature* root_signature = nullptr;
	ID3D12Debug* debug = nullptr;
	ID3D12CommandQueue* cmd_queue = nullptr;
	u64 query_frequency = 1;
	BufferHandle current_indirect_buffer = INVALID_BUFFER;
	BufferHandle current_index_buffer = INVALID_BUFFER;
	ProgramHandle current_program = INVALID_PROGRAM;
	PSOCache pso_cache;
	Window windows[64];
	Window* current_window = windows;
	FrameBuffer current_framebuffer;
	Array<Frame> frames;
	Frame* frame;
	ID3D12GraphicsCommandList* cmd_list = nullptr;
	HMODULE d3d_dll;
	HMODULE dxgi_dll;
	SRVUAVHeap srv_heap;
	ID3D12QueryHeap* timestamp_query_heap;
	ID3D12QueryHeap* stats_query_heap;
	u32 timestamp_query_count = 0;
	u32 stats_query_count = 0;
	SamplerHeap sampler_heap;
	RTVDSVHeap rtv_heap;
	RTVDSVHeap ds_heap;
	ShaderCompiler shader_compiler;
	D3D12_GPU_VIRTUAL_ADDRESS uniform_blocks[6];
	u32 dirty_compute_uniform_blocks = 0;
	u32 dirty_gfx_uniform_blocks = 0;
	u64 frame_number = 0;
	u32 debug_groups_depth = 0;
	StaticString<128> debug_groups_queue[8];
	D3D12_GPU_DESCRIPTOR_HANDLE bound_shader_buffers = {};

	bool vsync = true;
	bool vsync_dirty = false;
	Mutex vsync_mutex;
	Mutex disassembly_mutex;
};

static Local<D3D> d3d;

void* getDX12CommandList() {
	return d3d->cmd_list;
}

void* getDX12Device() {
	return d3d->device;
}

void* getDX12Resource(TextureHandle h) {
	return h->resource;
}

void barrierWrite(BufferHandle buffer) {
	buffer->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void barrierRead(BufferHandle buffer) {
	buffer->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void barrierWrite(TextureHandle texture) {
	texture->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void barrierRead(TextureHandle texture) {
	if (isDepthFormat(texture->dxgi_format)) {
		texture->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_DEPTH_READ);
	}
	else {
		texture->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_GENERIC_READ);
	}
}

void memoryBarrier(BufferHandle buffer) {
	D3D12_RESOURCE_BARRIER barrier;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = buffer->resource;
	d3d->cmd_list->ResourceBarrier(1, &barrier);
}

void memoryBarrier(TextureHandle texture) {
	ASSERT(isFlagSet(texture->flags, TextureFlags::COMPUTE_WRITE));
	D3D12_RESOURCE_BARRIER barrier;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = texture->resource;
	d3d->cmd_list->ResourceBarrier(1, &barrier);
}

void Frame::end(ID3D12CommandQueue* cmd_queue, ID3D12GraphicsCommandList* cmd_list, ID3D12QueryHeap* timestamp_query_heap, ID3D12QueryHeap* stats_query_heap) {
	timestamp_query_buffer->Unmap(0, nullptr);
	for (u32 i = 0, c = to_resolve.size(); i < c; ++i) {
		QueryHandle q = to_resolve[i];
		cmd_list->ResolveQueryData(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, q->idx, 1, timestamp_query_buffer, i * 8);
	}

	stats_query_buffer->Unmap(0, nullptr);
	for (u32 i = 0, c = to_resolve_stats.size(); i < c; ++i) {
		QueryHandle q = to_resolve_stats[i];
		cmd_list->ResolveQueryData(stats_query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, q->idx, 1, stats_query_buffer, i * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
	}

	HRESULT hr = cmd_list->Close();
	ASSERT(hr == S_OK);
	hr = cmd_queue->Wait(fence, fence_value);
	ASSERT(hr == S_OK);
	if (capture_requested) {
		if (d3d->rdoc_api) {
			if (!d3d->rdoc_api->IsRemoteAccessConnected()) {
				d3d->rdoc_api->LaunchReplayUI(1, "");
			}
			d3d->rdoc_api->TriggerCapture();
		}
		//if (PIXIsAttachedForGpuCapture()) PIXBeginCapture(PIX_CAPTURE_GPU, {});
	}
	cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmd_list);
	if (capture_requested) {
		capture_requested = false;
		#ifdef LUMIX_DEBUG
			if (PIXIsAttachedForGpuCapture()) PIXEndCapture(FALSE);
		#endif
	}


	++fence_value;
	hr = cmd_queue->Signal(fence, fence_value);
	ASSERT(hr == S_OK);
}

void Frame::begin() {
	wait();
	timestamp_query_buffer->Map(0, nullptr, (void**)&timestamp_query_buffer_ptr);
	stats_query_buffer->Map(0, nullptr, (void**)&stats_query_buffer_ptr);

	for (u32 i = 0, c = to_resolve.size(); i < c; ++i) {
		QueryHandle q = to_resolve[i];
		memcpy(&q->result, timestamp_query_buffer_ptr + i * 8, sizeof(q->result));
		q->ready = true;
	}
	to_resolve.clear();

	for (u32 i = 0, c = to_resolve_stats.size(); i < c; ++i) {
		QueryHandle q = to_resolve_stats[i];
		D3D12_QUERY_DATA_PIPELINE_STATISTICS stats;
		memcpy(&stats, stats_query_buffer_ptr + i * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS), sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
		q->result = stats.CInvocations;
		q->ready = true;
	}
	to_resolve_stats.clear();

	for (IUnknown* res : to_release) res->Release();
	for (u32 i : to_heap_release) d3d->srv_heap.free(i);
	to_release.clear();
	to_heap_release.clear();

	for (const TextureRead& read : texture_reads) {
		u8* src = nullptr; 
		HRESULT hr = read.staging->Map(0, nullptr, (void**)&src);
		if (src && hr == S_OK) {
			u8* dst = (u8*)d3d->allocator.allocate(read.dst_total_bytes, 16);
			u8* dst_start = dst;
			for (u32 i = 0; i < read.num_layouts; ++i) {
				const auto& footprint = read.layouts[i].Footprint;
				u32 dst_row_size = footprint.Width * getSize(footprint.Format);
				for (u32 row = 0; row < footprint.Height; ++row) {
					memcpy(dst, src + read.layouts[i].Offset + row * footprint.RowPitch, dst_row_size);
					dst += dst_row_size;
				}
			}
			read.callback.invoke(Span(dst_start, read.dst_total_bytes));
			d3d->allocator.deallocate(dst_start);
			read.staging->Unmap(0, nullptr);
		}
		read.staging->Release();
	}
	texture_reads.clear();
}

void Frame::clear() {
	for (IUnknown* res : to_release) res->Release();
	for (u32 i : to_heap_release) d3d->srv_heap.free(i);
	fence->Release();
		
	to_release.clear();
	to_heap_release.clear();

	scratch_buffer->Release();
	timestamp_query_buffer->Release();
	stats_query_buffer->Release();
}

void captureFrame() {
	d3d->frame->capture_requested = true;
}

static void tryLoadRenderDoc() {
	HMODULE lib = LoadLibrary("renderdoc.dll");
	if (!lib) lib = LoadLibrary("C:\\Program Files\\RenderDoc\\renderdoc.dll");
	if (!lib) return;
	pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(lib, "RENDERDOC_GetAPI");
	if (RENDERDOC_GetAPI) {
		RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_2, (void**)&d3d->rdoc_api);
		d3d->rdoc_api->MaskOverlayBits(~RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled, 0);
	}
	// FreeLibrary(lib);
}

QueryHandle createQuery(QueryType type) {
	checkThread();
	switch(type) {
		case QueryType::STATS: {
			ASSERT(d3d->stats_query_count < STATS_QUERY_COUNT);
			Query* q = LUMIX_NEW(d3d->allocator, Query);
			q->idx = d3d->stats_query_count;
			q->type = type;
			++d3d->stats_query_count;
			return q;
		}
		case QueryType::TIMESTAMP: {
			ASSERT(d3d->timestamp_query_count < TIMESTAMP_QUERY_COUNT);
			Query* q = LUMIX_NEW(d3d->allocator, Query);
			q->type = type;
			q->idx = d3d->timestamp_query_count;
			++d3d->timestamp_query_count;
			return q;
		}
		default: ASSERT(false); return INVALID_QUERY;
	}
}

void checkThread() {
	ASSERT(d3d->thread == GetCurrentThreadId());
}

void destroy(ProgramHandle program) {
	checkThread();

	ASSERT(program);
	LUMIX_DELETE(d3d->allocator, program);
}

void destroy(TextureHandle texture) {
	checkThread();
	ASSERT(texture);
	Texture& t = *texture;
	if (t.resource && !t.is_view) d3d->frame->to_release.push(t.resource);
	if (t.heap_id != INVALID_HEAP_ID) d3d->frame->to_heap_release.push(t.heap_id);
	LUMIX_DELETE(d3d->allocator, texture);
}

void destroy(QueryHandle query) {
	checkThread();
	LUMIX_DELETE(d3d->allocator, query);
}

void update(TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, TextureFormat format, const void* buf, u32 buf_size) {
	const D3D12_RESOURCE_STATES prev_state = texture->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_DEST);

	const FormatDesc& fd = FormatDesc::get(format);
	D3D12_RESOURCE_DESC desc = texture->resource->GetDesc();
	if (fd.compressed) {
		w = (w + 3) & ~3;
		h = (h + 3) & ~3;
	}
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;

	u32 num_rows;
	u64 total_bytes;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
	d3d->device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &num_rows, NULL, &total_bytes);

	const u32 tmp_row_pitch = layout.Footprint.RowPitch;

	ID3D12Resource* staging = createBuffer(d3d->device, nullptr, total_bytes, D3D12_HEAP_TYPE_UPLOAD);
	u8* tmp;

	staging->Map(0, nullptr, (void**)&tmp);

	const u32 src_pitch = fd.getRowPitch(w);
	for (u32 i = 0, height = num_rows; i < height; ++i) {
		memcpy(&tmp[i * tmp_row_pitch], &((u8*)buf)[i * src_pitch], src_pitch);
	}

	staging->Unmap(0, nullptr);

	D3D12_BOX box;
	box.left = 0;
	box.top = 0;
	box.right = w;
	box.bottom = h;
	box.front = 0;
	box.back = 1;

	const bool no_mips = u32(texture->flags & TextureFlags::NO_MIPS);
	const u32 mip_count = no_mips ? 1 : 1 + log2(maximum(texture->w, texture->h));

	D3D12_TEXTURE_COPY_LOCATION dst = {texture->resource, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
	dst.SubresourceIndex = z * mip_count + mip;
	D3D12_TEXTURE_COPY_LOCATION src = {staging, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {layout}};
	d3d->cmd_list->CopyTextureRegion(&dst, x, y, 0, &src, &box);

	texture->setState(d3d->cmd_list, prev_state);

	d3d->frame->to_release.push(staging);
}

void copy(BufferHandle dst_handle, TextureHandle src_handle) {
	D3D12_RESOURCE_DESC desc = src_handle->resource->GetDesc();

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
	u32 num_rows;
	u64 total;
	d3d->device->GetCopyableFootprints(&desc
		, calcSubresource(0, 0, desc.MipLevels)
		, 1
		, 0
		, &layout
		, &num_rows
		, NULL
		, &total
	);
	u32 src_pitch = layout.Footprint.RowPitch;

	D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
	srcLocation.pResource = src_handle->resource;
	srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	bool is_cubemap = isFlagSet(src_handle->flags, gpu::TextureFlags::IS_CUBE);
	bool no_mips = isFlagSet(src_handle->flags, gpu::TextureFlags::NO_MIPS);

	D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
	dstLocation.pResource = dst_handle->resource;
	dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

	dstLocation.PlacedFootprint = layout;
	dstLocation.PlacedFootprint.Offset = 0;
	
	const u32 src_mip_count = no_mips ? 1 : 1 + log2(maximum(src_handle->w, src_handle->h));

	for (int i = 0; i < (is_cubemap ? 6 : 1); ++i) {
		srcLocation.SubresourceIndex = calcSubresource(0, i, src_mip_count);
		dstLocation.PlacedFootprint.Offset = i * src_pitch * src_handle->h;

		D3D12_BOX box;
		box.left = 0;
		box.top = 0;
		box.right = (u32)desc.Width;
		box.bottom = (u32)desc.Height;
		box.front = 0;
		box.back = 1;

		d3d->cmd_list->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, &box);
	}
}

void copy(TextureHandle dst, TextureHandle src, u32 dst_x, u32 dst_y) {
	ASSERT(dst);
	ASSERT(src);

	const bool no_mips = u32(src->flags & TextureFlags::NO_MIPS);
	const u32 src_mip_count = no_mips ? 1 : 1 + log2(maximum(src->w, src->h));
	const u32 dst_mip_count = no_mips ? 1 : 1 + log2(maximum(dst->w, dst->h));

	const D3D12_RESOURCE_STATES src_prev_state = src->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_SOURCE);
	const D3D12_RESOURCE_STATES dst_prev_state = dst->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_DEST);

	u32 mip = 0;
	while ((src->w >> mip) != 0 || (src->h >> mip) != 0) {
		const u32 w = maximum(src->w >> mip, 1);
		const u32 h = maximum(src->h >> mip, 1);

		if (u32(src->flags & TextureFlags::IS_CUBE)) {
			ASSERT(false); // TODO
			for (u32 face = 0; face < 6; ++face) {
				const UINT src_subres = mip +  face * src_mip_count;
				const UINT dst_subres = mip +  face * dst_mip_count;
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				D3D12_TEXTURE_COPY_LOCATION src = {};
				D3D12_BOX src_box;
				d3d->cmd_list->CopyTextureRegion(&dst, dst_x, dst_y, 0, &src, &src_box);
				//d3d->cmd_list->CopyTextureRegion(dst->texture2D, dst_subres, dst_x, dst_y, 0, src->texture2D, src_subres, nullptr);
			}
		}
		else {
			D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
			D3D12_TEXTURE_COPY_LOCATION src_loc = {};
			src_loc.pResource = src->resource;
			src_loc.SubresourceIndex = mip;
			dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst_loc.pResource = dst->resource;
			dst_loc.SubresourceIndex = mip;
			dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			d3d->cmd_list->CopyTextureRegion(&dst_loc, dst_x, dst_y, 0, &src_loc, nullptr);
		}
		++mip;
		if (u32(src->flags & TextureFlags::NO_MIPS)) break;
		if (u32(dst->flags & TextureFlags::NO_MIPS)) break;
	}
	src->setState(d3d->cmd_list, src_prev_state);
	dst->setState(d3d->cmd_list, dst_prev_state);
}

void readTexture(TextureHandle texture, TextureReadCallback callback) {
	const D3D12_RESOURCE_DESC desc = texture->resource->GetDesc();
	bool is_cubemap = isFlagSet(texture->flags, gpu::TextureFlags::IS_CUBE);

	u64 face_bytes = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[16 * 6];
	ASSERT(desc.MipLevels <= (int)lengthOf(layouts));
	d3d->device->GetCopyableFootprints(&desc
		, 0
		, desc.MipLevels * desc.DepthOrArraySize 
		, 0
		, layouts
		, nullptr
		, nullptr
		, &face_bytes
	);

	ID3D12Resource* staging = createBuffer(d3d->device, nullptr, face_bytes * (is_cubemap ? 6 : 1), D3D12_HEAP_TYPE_READBACK);
	const D3D12_RESOURCE_STATES prev_state = texture->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_SOURCE);
	
	D3D12_TEXTURE_COPY_LOCATION src_location = {};
	src_location.pResource = texture->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

	D3D12_TEXTURE_COPY_LOCATION dst_location = {};
	dst_location.pResource = staging;
	dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

	for (int face = 0; face < (is_cubemap ? 6 : 1); ++face) {
		for (u32 mip = 0; mip < desc.MipLevels; ++mip) {
			src_location.SubresourceIndex = calcSubresource(mip, face, desc.MipLevels);
			const auto& layout = layouts[src_location.SubresourceIndex];
			dst_location.PlacedFootprint = layout;
			//dst_location.PlacedFootprint.Offset += face * face_bytes;

			D3D12_BOX box;
			box.left = 0;
			box.top = 0;
			box.right = layout.Footprint.Width;
			box.bottom = layout.Footprint.Height;
			box.front = 0;
			box.back = 1;

			d3d->cmd_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
		}
	}
	texture->setState(d3d->cmd_list, prev_state);

	Frame::TextureRead& wait = d3d->frame->texture_reads.emplace();
	wait.staging = staging;
	wait.callback = callback;
	wait.num_layouts = desc.DepthOrArraySize * desc.MipLevels;
	wait.dst_total_bytes = 0;
	for (u32 mip = 0; mip < desc.MipLevels; ++mip) {
		const auto& footprint = layouts[mip].Footprint;
		wait.dst_total_bytes += u32(footprint.Width * footprint.Height * desc.DepthOrArraySize * getSize(desc.Format));
	}
	memcpy(wait.layouts, layouts, sizeof(layouts[0]) * wait.num_layouts);
}

void beginQuery(QueryHandle query) {
	checkThread();
	ASSERT(query);
	query->ready = false;
	d3d->cmd_list->BeginQuery(d3d->stats_query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, query->idx);
}

void endQuery(QueryHandle query) {
	checkThread();
	ASSERT(query);
	d3d->frame->to_resolve_stats.push(query);
	d3d->cmd_list->EndQuery(d3d->stats_query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, query->idx);
}

void queryTimestamp(QueryHandle query) {
	checkThread();
	ASSERT(query);
	query->ready = false;
	d3d->frame->to_resolve.push(query);
	d3d->cmd_list->EndQuery(d3d->timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, query->idx);
}

u64 getQueryFrequency() {
	return d3d->query_frequency;
}

u64 getQueryResult(QueryHandle query) {
	checkThread();
	ASSERT(query);
	ASSERT(query->ready);
	return query->result;
}

bool isQueryReady(QueryHandle query) {
	checkThread();
	ASSERT(query);
	return query->ready;
}

void preinit(IAllocator& allocator, bool load_renderdoc) {
	d3d.create(allocator);
	d3d->srv_heap.preinit(MAX_SRV_DESCRIPTORS, 1024);
	if (load_renderdoc) tryLoadRenderDoc();

	for (u32 i = 0; i < NUM_BACKBUFFERS; ++i) {
		d3d->frames.push(allocator);
	}
	d3d->frame = d3d->frames.begin();
}

void shutdown() {
	d3d->shader_compiler.saveCache(".lumix/shader_cache_dx");

	for (Frame& frame : d3d->frames) {
		frame.clear();
	}
	d3d->frames.clear();

	for (D3D::Window& w : d3d->windows) {
		if (!w.handle) continue;
		w.swapchain->Release();
	}
	
	d3d->root_signature->Release();
	d3d->timestamp_query_heap->Release();
	d3d->stats_query_heap->Release();
	d3d->cmd_queue->Release();
	d3d->cmd_list->Release();
	if(d3d->debug) d3d->debug->Release();
	d3d->device->Release();

	#ifdef LUMIX_DEBUG
		auto api_DXGIGetDebugInterface1 = (decltype(DXGIGetDebugInterface1)*)GetProcAddress(d3d->dxgi_dll, "DXGIGetDebugInterface1");
		IDXGIDebug1* dxgi_debug;
		if (DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug)) == S_OK) {
			dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	#endif

	FreeLibrary(d3d->d3d_dll);
	FreeLibrary(d3d->dxgi_dll);
	d3d.destroy();
}

ID3D12RootSignature* createRootSignature() {
	PROFILE_FUNCTION();

	constexpr u32 MAX_CBV = 16;
	D3D12_DESCRIPTOR_RANGE bindless_srv_desc_ranges[] = {
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)-1, 0, 1, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)-1, 0, 2, 0},
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)-1, 0, 3, 0},
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)-1, 0, 4, 0},
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)-1, 0, 5, 0},
		{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, (UINT)-1, 0, 0, 0},
		{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, (UINT)-1, 0, 1, 0},
	};

	D3D12_DESCRIPTOR_RANGE srv_desc_range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 16, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
	D3D12_DESCRIPTOR_RANGE sampler_desc_range = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2048, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};

	D3D12_ROOT_PARAMETER rootParameter[] = {
		{D3D12_ROOT_PARAMETER_TYPE_CBV, {{0, 0}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_CBV, {{0, 0}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_CBV, {{0, 0}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_CBV, {{0, 0}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_CBV, {{0, 0}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_CBV, {{0, 0}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {{lengthOf(bindless_srv_desc_ranges), bindless_srv_desc_ranges}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {{1, &sampler_desc_range}}, D3D12_SHADER_VISIBILITY_ALL},
		{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {{1, &srv_desc_range}}, D3D12_SHADER_VISIBILITY_ALL},
	};
	rootParameter[0].Descriptor.RegisterSpace = 0;
	rootParameter[0].Descriptor.ShaderRegister = 0;
	rootParameter[1].Descriptor.RegisterSpace = 0;
	rootParameter[1].Descriptor.ShaderRegister = 1;
	rootParameter[2].Descriptor.RegisterSpace = 0;
	rootParameter[2].Descriptor.ShaderRegister = 2;
	rootParameter[3].Descriptor.RegisterSpace = 0;
	rootParameter[3].Descriptor.ShaderRegister = 3;
	rootParameter[4].Descriptor.RegisterSpace = 0;
	rootParameter[4].Descriptor.ShaderRegister = 4;
	rootParameter[5].Descriptor.RegisterSpace = 0;
	rootParameter[5].Descriptor.ShaderRegister = 5;

	D3D12_ROOT_SIGNATURE_DESC desc;
	desc.NumParameters = lengthOf(rootParameter);
	desc.pParameters = rootParameter;
	desc.NumStaticSamplers = 0;
	desc.pStaticSamplers = nullptr; //&staticSampler;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

#define DECL_D3D_API(f) auto api_##f = (decltype(f)*)GetProcAddress(d3d->d3d_dll, #f);

	DECL_D3D_API(D3D12SerializeRootSignature);

	ID3DBlob* blob = NULL;
	ID3DBlob* error = NULL;

	HRESULT hr = api_D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if (error) {
		const char* msg = (const char*)error->GetBufferPointer();
		ASSERT(false);
	}
	if (hr != S_OK) return nullptr;

	ID3D12RootSignature* res;
	if (d3d->device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&res)) != S_OK) {
		blob->Release();
		return nullptr;
	}
	blob->Release();
	return res;
}

// TODO srgb window swapchain views
[[nodiscard]] static bool createSwapchain(HWND hwnd, D3D::Window& window, bool vsync) {
	PROFILE_FUNCTION();
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.BufferCount = NUM_BACKBUFFERS;
	sd.Width = window.size.x;
	sd.Height = window.size.y;
	sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (vsync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.Stereo = FALSE;

	IDXGIFactory4* dxgi_factory = NULL;
	IDXGISwapChain1* swapChain1 = NULL;

	if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory)) != S_OK) return false;
	if (dxgi_factory->CreateSwapChainForHwnd(d3d->cmd_queue, (HWND)hwnd, &sd, NULL, NULL, &swapChain1) != S_OK) return false;
	if (swapChain1->QueryInterface(IID_PPV_ARGS(&window.swapchain)) != S_OK) return false;

	swapChain1->Release();
	dxgi_factory->Release();
	window.swapchain->SetMaximumFrameLatency(1);

	for (u32 i = 0; i < NUM_BACKBUFFERS; ++i) {
		ID3D12Resource* backbuffer;
		if (window.swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffer)) != S_OK) return false;
		backbuffer->SetName(L"window_rb");
		window.backbuffers[i] = backbuffer;
	}

	const UINT current_bb_idx = window.swapchain->GetCurrentBackBufferIndex();
	switchState(d3d->cmd_list, window.backbuffers[current_bb_idx], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
	return true;
}

bool init(void* hwnd, InitFlags flags) {
	PROFILE_FUNCTION();
	bool debug = u32(flags & InitFlags::DEBUG_OUTPUT);
#ifdef LUMIX_DEBUG
	debug = true;
#endif

	d3d->vsync = true;
	d3d->thread = GetCurrentThreadId();

	RECT rect;
	GetClientRect((HWND)hwnd, &rect);
	d3d->windows[0].size = IVec2(rect.right - rect.left, rect.bottom - rect.top);
	d3d->windows[0].handle = hwnd;
	d3d->current_window = &d3d->windows[0];

	const int width = rect.right - rect.left;
	const int height = rect.bottom - rect.top;
	{
		PROFILE_BLOCK("load libs");
		d3d->d3d_dll = LoadLibrary("d3d12.dll");
		d3d->dxgi_dll = LoadLibrary("dxgi.dll");
	}
	if (!d3d->d3d_dll) {
		logError("Failed to load d3d11.dll");
		return false;
	}
	if (!d3d->dxgi_dll) {
		logError("Failed to load dxgi.dll");
		return false;
	}

	#define DECL_D3D_API(f) auto api_##f = (decltype(f)*)GetProcAddress(d3d->d3d_dll, #f);

	DECL_D3D_API(D3D12CreateDevice);
	DECL_D3D_API(D3D12GetDebugInterface);

	if (debug) {
		if (api_D3D12GetDebugInterface(IID_PPV_ARGS(&d3d->debug)) != S_OK) return false;
		d3d->debug->EnableDebugLayer();

		//ID3D12Debug1* debug1;
		//d3d->debug->QueryInterface(IID_PPV_ARGS(&debug1));
		//debug1->SetEnableGPUBasedValidation(true);
	}

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;
	HRESULT hr = api_D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&d3d->device));
	if (!SUCCEEDED(hr)) {
		logError("DX12 CreateDevice failed.");
		return false;
	}

	if (debug) {
		ID3D12InfoQueue* info_queue;
		hr = d3d->device->QueryInterface(IID_PPV_ARGS(&info_queue));
		if (SUCCEEDED(hr)) {
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
			D3D12_INFO_QUEUE_FILTER filter = {};

			D3D12_MESSAGE_CATEGORY catlist[] = {
				D3D12_MESSAGE_CATEGORY_STATE_CREATION,
			};
			filter.DenyList.NumCategories = 0;
			filter.DenyList.pCategoryList = nullptr;

			D3D12_MESSAGE_ID idlist[] = {
				D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, 
				D3D12_MESSAGE_ID_CREATEINPUTLAYOUT_EMPTY_LAYOUT,
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE
			};
			filter.DenyList.NumIDs = lengthOf(idlist);
			filter.DenyList.pIDList = idlist;
			filter.DenyList.NumSeverities = 1;
			D3D12_MESSAGE_SEVERITY info_severity = D3D12_MESSAGE_SEVERITY_INFO;
			filter.DenyList.pSeverityList = &info_severity;
			info_queue->PushStorageFilter(&filter);
		}
	}

	d3d->root_signature = createRootSignature();
	ASSERT(d3d->root_signature);

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 1;

	if (d3d->device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d->cmd_queue)) != S_OK) return false;

	if (!d3d->srv_heap.init(d3d->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return false;
	if (!d3d->sampler_heap.init(d3d->device, 2048)) return false;
	if (!d3d->rtv_heap.init(d3d->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024)) return false;
	if (!d3d->ds_heap.init(d3d->device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256)) return false;

	for (Frame& f : d3d->frames) {
		if (!f.init(d3d->device)) return false;
	}

	if (d3d->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3d->frames[0].cmd_allocator, NULL, IID_PPV_ARGS(&d3d->cmd_list)) != S_OK) return false;
	d3d->cmd_list->Close();

	d3d->frame->timestamp_query_buffer->Map(0, nullptr, (void**)&d3d->frame->timestamp_query_buffer_ptr);
	d3d->frame->stats_query_buffer->Map(0, nullptr, (void**)&d3d->frame->stats_query_buffer_ptr);
	d3d->frame->cmd_allocator->Reset();
	d3d->cmd_list->Reset(d3d->frame->cmd_allocator, nullptr);
	d3d->cmd_list->SetGraphicsRootSignature(d3d->root_signature);
	d3d->cmd_list->SetComputeRootSignature(d3d->root_signature);
	ID3D12DescriptorHeap* heaps[] = {d3d->srv_heap.heap, d3d->sampler_heap.heap };
	d3d->cmd_list->SetDescriptorHeaps(lengthOf(heaps), heaps);

	if (!createSwapchain((HWND)hwnd, d3d->windows[0], d3d->vsync)) return false;

	for (TextureHandle& h : d3d->current_framebuffer.attachments) h = INVALID_TEXTURE;

	d3d->shader_compiler.loadCache(".lumix/shader_cache_dx");

	{
		D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
		queryHeapDesc.Count = TIMESTAMP_QUERY_COUNT;
		queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		if (d3d->device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&d3d->timestamp_query_heap)) != S_OK) return false;
		HRESULT freq_hr = d3d->cmd_queue->GetTimestampFrequency(&d3d->query_frequency);
		if (FAILED(freq_hr)) {
			logError("failed to get timestamp frequency, GPU timing will most likely be wrong");
			d3d->query_frequency = 1'000'000'000;
		}
	}

	{
		D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
		queryHeapDesc.Count = STATS_QUERY_COUNT;
		queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
		if (d3d->device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&d3d->stats_query_heap)) != S_OK) return false;
	}

	return true;
}

void pushDebugGroup(const char* msg) {
	#ifdef LUMIX_DEBUG
		if (d3d->debug_groups_depth < lengthOf(d3d->debug_groups_queue)) {
			d3d->debug_groups_queue[d3d->debug_groups_depth] = msg;
		}
		++d3d->debug_groups_depth;
		WCHAR tmp[128];
		toWChar(tmp, msg);
		PIXBeginEvent(d3d->cmd_list, PIX_COLOR(0x55, 0xff, 0x55), tmp);
	#endif
}

void popDebugGroup() {
	#ifdef LUMIX_DEBUG
		--d3d->debug_groups_depth;
		PIXEndEvent(d3d->cmd_list);
	#endif
}

void setFramebufferCube(TextureHandle cube, u32 face, u32 mip) {
	d3d->pso_cache.last = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE rt;

	D3D12_RENDER_TARGET_VIEW_DESC desc = {};
	desc.Format = cube->dxgi_format;
	desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	desc.Texture2DArray.MipSlice = mip;
	desc.Texture2DArray.ArraySize = 1;
	desc.Texture2DArray.FirstArraySlice = face;

	rt = d3d->rtv_heap.allocRTV(d3d->device, cube->resource, &desc);
	d3d->current_framebuffer.count = 1;
	d3d->current_framebuffer.formats[0] = cube->dxgi_format;
	d3d->current_framebuffer.render_targets[0] = rt;
	d3d->current_framebuffer.depth_stencil = {};
	d3d->current_framebuffer.ds_format = DXGI_FORMAT_UNKNOWN;
	cube->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_RENDER_TARGET);
	d3d->cmd_list->OMSetRenderTargets(1, &rt, FALSE, nullptr);
}

void setFramebuffer(const TextureHandle* attachments, u32 num, TextureHandle depth_stencil, FramebufferFlags flags) {
	checkThread();
	d3d->pso_cache.last = nullptr;

	for (TextureHandle& texture : d3d->current_framebuffer.attachments) {
		if (texture) texture->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	const bool readonly_ds = u32(flags & FramebufferFlags::READONLY_DEPTH_STENCIL);
	if (num == 0 && !depth_stencil) {
		d3d->current_framebuffer.count = 1;
		d3d->current_framebuffer.formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		d3d->current_framebuffer.render_targets[0] = d3d->rtv_heap.allocRTV(d3d->device, d3d->current_window->backbuffers[d3d->current_window->swapchain->GetCurrentBackBufferIndex()]);
		d3d->current_framebuffer.depth_stencil = {};
		d3d->current_framebuffer.ds_format = DXGI_FORMAT_UNKNOWN;
	} else {
		d3d->current_framebuffer.count = 0;
		for (u32 i = 0; i < num; ++i) {
			d3d->current_framebuffer.attachments[i] = attachments[i];
			ASSERT(attachments[i]);
			Texture& t = *attachments[i];
			ASSERT(d3d->current_framebuffer.count < (u32)lengthOf(d3d->current_framebuffer.render_targets));
			t.setState(d3d->cmd_list, D3D12_RESOURCE_STATE_RENDER_TARGET);
			d3d->current_framebuffer.formats[d3d->current_framebuffer.count] = t.dxgi_format;
			d3d->current_framebuffer.render_targets[d3d->current_framebuffer.count] = d3d->rtv_heap.allocRTV(d3d->device, t.resource);
			++d3d->current_framebuffer.count;
		}
		if (depth_stencil) {
			depth_stencil->setState(d3d->cmd_list, readonly_ds ? D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_DEPTH_WRITE);
			d3d->current_framebuffer.depth_stencil = d3d->ds_heap.allocDSV(d3d->device, *depth_stencil);
			d3d->current_framebuffer.ds_format = toDSViewFormat(depth_stencil->dxgi_format);
		}
		else {
			d3d->current_framebuffer.depth_stencil = {};
			d3d->current_framebuffer.ds_format = DXGI_FORMAT_UNKNOWN;
		}
	}
	D3D12_CPU_DESCRIPTOR_HANDLE* ds = d3d->current_framebuffer.depth_stencil.ptr ? &d3d->current_framebuffer.depth_stencil : nullptr;
	d3d->cmd_list->OMSetRenderTargets(d3d->current_framebuffer.count, d3d->current_framebuffer.render_targets, FALSE, ds);
}

void clear(ClearFlags flags, const float* color, float depth) {
	if (u32(flags & ClearFlags::COLOR)) {
		for (u32 i = 0; i < d3d->current_framebuffer.count; ++i) {
			d3d->cmd_list->ClearRenderTargetView(d3d->current_framebuffer.render_targets[i], color, 0, nullptr);
		}
	}

	D3D12_CLEAR_FLAGS dx_flags = {};
	if (u32(flags & ClearFlags::DEPTH)) {
		dx_flags |= D3D12_CLEAR_FLAG_DEPTH;
	}
	if (u32(flags & ClearFlags::STENCIL)) {
		dx_flags |= D3D12_CLEAR_FLAG_STENCIL;
	}
	if (dx_flags && d3d->current_framebuffer.depth_stencil.ptr) {
		d3d->cmd_list->ClearDepthStencilView(d3d->current_framebuffer.depth_stencil, dx_flags, depth, 0, 0, nullptr);
	}
}

void* map(BufferHandle buffer, size_t size) {
	ASSERT(buffer);
	ASSERT(!buffer->mapped_ptr);
	HRESULT hr = buffer->resource->Map(0, nullptr, (void**)&buffer->mapped_ptr);
	// if you get random device removal errors here, see: 
	// https://github.com/microsoft/D3D11On12/issues/25
	// it's a bug in debug layer, either disable debug layer or use new sdk version - agility SDK or Win 11
	ASSERT(hr == S_OK);
	ASSERT(buffer->mapped_ptr);
	return buffer->mapped_ptr;
}

void unmap(BufferHandle buffer) {
	ASSERT(buffer);
	ASSERT(buffer->mapped_ptr);
	D3D12_RANGE range = {};
	buffer->resource->Unmap(0, &range);
	buffer->mapped_ptr = nullptr;
}

bool getMemoryStats(MemoryStats& stats) {
	return false;
}

void setCurrentWindow(void* window_handle) {
	checkThread();

	bool vsync = []() {
		MutexGuard guard(d3d->vsync_mutex);
		return d3d->vsync;
	}();

	if (!window_handle) {
		d3d->current_window = &d3d->windows[0];
		d3d->current_window->last_used_frame = d3d->frame_number;
		return;
	}

	for (auto& window : d3d->windows) {
		if (window.handle == window_handle) {
			d3d->current_window = &window;
			d3d->current_window->last_used_frame = d3d->frame_number;
			return;
		}
	}

	for (auto& window : d3d->windows) {
		if (window.handle) continue;

		window.handle = window_handle;
		d3d->current_window = &window;
		d3d->current_window->last_used_frame = d3d->frame_number;
		RECT rect;
		GetClientRect((HWND)window_handle, &rect);
		window.size = IVec2(rect.right - rect.left, rect.bottom - rect.top);

		if (!createSwapchain((HWND)window_handle, window, vsync)) {
			logError("Failed to create swapchain");
		}
		return;
	}

	logError("Too many windows created.");
	ASSERT(false);
}

bool frameFinished(u32 frame_idx) {
	Frame& f = d3d->frames.begin()[frame_idx];
	return f.isFinished();
}

void waitFrame(u32 frame_idx) {
	Frame& f = d3d->frames.begin()[frame_idx];
	f.wait();
}

bool isVSyncEnabled() {
	MutexGuard guard(d3d->vsync_mutex);
	return d3d->vsync;
}

void enableVSync(bool enable) {
	MutexGuard guard(d3d->vsync_mutex);
	d3d->vsync = enable;
	d3d->vsync_dirty = true;
}

u32 present() {
	d3d->vsync_mutex.enter();
	const bool vsync = d3d->vsync;
	const bool vsync_dirty = d3d->vsync_dirty;
	d3d->vsync_dirty = false;
	d3d->vsync_mutex.exit();

	d3d->pso_cache.last = nullptr;
	for (auto& window : d3d->windows) {
		if (!window.handle) continue;

		if (window.last_used_frame == d3d->frame_number || &window == d3d->windows) {
			const UINT current_idx = window.swapchain->GetCurrentBackBufferIndex();
			switchState(d3d->cmd_list, window.backbuffers[current_idx], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		}
	}

	d3d->frame->end(d3d->cmd_queue, d3d->cmd_list, d3d->timestamp_query_heap, d3d->stats_query_heap);
	const u32 frame_idx = u32(d3d->frame - d3d->frames.begin());

	++d3d->frame;
	if (d3d->frame >= d3d->frames.end()) d3d->frame = d3d->frames.begin();

	d3d->srv_heap.nextFrame();
	d3d->rtv_heap.nextFrame();
	d3d->ds_heap.nextFrame();

	for (auto& window : d3d->windows) {
		if (!window.handle) continue;
		if (window.last_used_frame + 2 < d3d->frame_number && &window != d3d->windows) {
			window.handle = nullptr;
			for (ID3D12Resource* res : window.backbuffers) res->Release();
			window.swapchain->Release();
		}
	}
	++d3d->frame_number;

	d3d->frame->begin();
	for (TextureHandle& h : d3d->current_framebuffer.attachments) h = INVALID_TEXTURE;

	d3d->frame->scratch_buffer_ptr = d3d->frame->scratch_buffer_begin;
	d3d->frame->cmd_allocator->Reset();
	d3d->cmd_list->Reset(d3d->frame->cmd_allocator, nullptr);
	d3d->cmd_list->SetGraphicsRootSignature(d3d->root_signature);
	d3d->cmd_list->SetComputeRootSignature(d3d->root_signature);
	ID3D12DescriptorHeap* heaps[] = { d3d->srv_heap.heap, d3d->sampler_heap.heap };
	d3d->cmd_list->SetDescriptorHeaps(lengthOf(heaps), heaps);

	for (auto& window : d3d->windows) {
		if (!window.handle) continue;
		if (window.last_used_frame + 1 != d3d->frame_number && &window != d3d->windows) continue;
		
		RECT rect;
		GetClientRect((HWND)window.handle, &rect);

		const IVec2 size(rect.right - rect.left, rect.bottom - rect.top);
		if (vsync_dirty) {
			for (Frame& f : d3d->frames) f.wait();

			for (ID3D12Resource* res : window.backbuffers) {
				res->Release();
			}
			window.swapchain->Release();
			window.last_used_frame = 0;
			if (!createSwapchain((HWND)window.handle, window, vsync)) {
				logError("Failed to create swapchain");
			}
		}
		else if ((size != window.size && size.x != 0)) {
			window.size = size;
			bool has_ds = false;

			for (Frame& f : d3d->frames) f.wait();

			for (ID3D12Resource* res : window.backbuffers) {
				res->Release();
			}

			HRESULT hr = window.swapchain->ResizeBuffers(0, size.x, size.y, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (vsync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
			ASSERT(hr == S_OK);

			SIZE_T rtvDescriptorSize = d3d->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			for (u32 i = 0; i < NUM_BACKBUFFERS; ++i) {
				hr = window.swapchain->GetBuffer(i, IID_PPV_ARGS(&window.backbuffers[i]));
				ASSERT(hr == S_OK);
				window.backbuffers[i]->SetName(L"window_rb");
			}
		}
	}

	if (!vsync_dirty) {
		for (auto& window : d3d->windows) {
			if (!window.handle) continue;
			if (window.last_used_frame + 1 != d3d->frame_number && &window != d3d->windows) continue;

			if (vsync) {
				window.swapchain->Present(1, 0);
			}
			else {
				window.swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
			}
		
			//DXGI_FRAME_STATISTICS stats;
			//window.swapchain->GetFrameStatistics(&stats);

			const UINT current_idx = window.swapchain->GetCurrentBackBufferIndex();
			switchState(d3d->cmd_list, window.backbuffers[current_idx], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		}
	}

	return frame_idx;
}

void createBuffer(BufferHandle buffer, BufferFlags flags, size_t size, const void* data, const char* debug_name) {
	ASSERT(buffer);
	ASSERT(!buffer->resource);
	ASSERT(size < UINT_MAX);
	buffer->size = (u32)size;
	#ifdef LUMIX_DEBUG
		buffer->name = debug_name;
	#endif
	const bool mappable = u32(flags & BufferFlags::MAPPABLE);
	const bool shader_buffer = isFlagSet(flags, BufferFlags::SHADER_BUFFER);
	if (shader_buffer) {
		size = ((size + 15) / 16) * 16;
	}	

	D3D12_HEAP_PROPERTIES props = {};
	props.Type = mappable ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = shader_buffer ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

	buffer->state = mappable ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;
	HRESULT hr = d3d->device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, buffer->state, NULL, IID_PPV_ARGS(&buffer->resource));
	ASSERT(hr == S_OK);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
	srv_desc = {};
	srv_desc.Format = DXGI_FORMAT_R32_UINT;
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srv_desc.Buffer.FirstElement = 0;
	srv_desc.Buffer.NumElements = UINT(size / sizeof(u32));
	srv_desc.Buffer.StructureByteStride = 0;
	srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (shader_buffer) {
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uav_desc.Format = DXGI_FORMAT_R32_UINT;
		uav_desc.Buffer.CounterOffsetInBytes = 0;
		uav_desc.Buffer.FirstElement = 0;
		uav_desc.Buffer.NumElements = srv_desc.Buffer.NumElements;
		uav_desc.Buffer.StructureByteStride = 0;
		uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		d3d->srv_heap.alloc(d3d->device, buffer->heap_id, buffer->resource, srv_desc, &uav_desc);
	}
	else {
		d3d->srv_heap.alloc(d3d->device, buffer->heap_id, buffer->resource, srv_desc, nullptr);
	}

	if (data) {
		ID3D12Resource* upload_buffer = createBuffer(d3d->device, data, size, D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_STATES old_state = buffer->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_DEST);
		d3d->cmd_list->CopyResource(buffer->resource, upload_buffer);
		buffer->setState(d3d->cmd_list, old_state);
		d3d->frame->to_release.push(upload_buffer);
	}
	if (debug_name) {
		WCHAR tmp[MAX_PATH];
		toWChar(tmp, debug_name);
		buffer->resource->SetName(tmp);
	}
}

ProgramHandle allocProgramHandle() {
	Program* p = LUMIX_NEW(d3d->allocator, Program)(d3d->allocator);
	return p;
}

BufferHandle allocBufferHandle() {
	BufferHandle b = LUMIX_NEW(d3d->allocator, Buffer);
	b->heap_id = d3d->srv_heap.reserveID();
	return b;
}

TextureHandle allocTextureHandle() {
	TextureHandle t = LUMIX_NEW(d3d->allocator, Texture);
	t->heap_id = d3d->srv_heap.reserveID();
	return t;
}

void createTextureView(TextureHandle view_handle, TextureHandle texture_handle, u32 layer, u32 mip) {
	Texture& texture = *texture_handle;
	Texture& view = *view_handle;
	view.dxgi_format = texture.dxgi_format;
	view.w = texture.w;
	view.h = texture.h;
	view.flags = texture.flags;
	view.resource = texture.resource;
	view.state = texture.state;
	view.is_view = true;
	
	const bool is_srgb = u32(texture.flags & TextureFlags::SRGB);
	const bool no_mips = u32(texture.flags & TextureFlags::NO_MIPS);
	const bool is_3d = u32(texture.flags & TextureFlags::IS_3D);
	const bool is_cubemap = u32(texture.flags & TextureFlags::IS_CUBE);
	const bool compute_write = u32(texture.flags & TextureFlags::COMPUTE_WRITE);
	const u32 mip_count = no_mips ? 1 : 1 + log2(maximum(view.w, view.h)) - mip;
	if (no_mips) mip = 0;
	ASSERT(!is_3d);
	
	if (is_cubemap) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = texture.dxgi_format;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srv_desc.Texture2DArray.MipLevels = mip_count;
		srv_desc.Texture2DArray.MostDetailedMip = mip;
		srv_desc.Texture2DArray.ArraySize = 1;
		srv_desc.Texture2DArray.FirstArraySlice = layer;
		srv_desc.Texture2DArray.ResourceMinLODClamp = 0;
		srv_desc.Texture2DArray.PlaneSlice = 0;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.Format = srv_desc.Format;
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uav_desc.Texture2DArray.ArraySize = 1;
		uav_desc.Texture2DArray.MipSlice = mip;
		uav_desc.Texture2DArray.PlaneSlice = 0;
		uav_desc.Texture2DArray.FirstArraySlice = layer;

		d3d->srv_heap.alloc(d3d->device, view.heap_id, texture.resource, srv_desc, compute_write ? &uav_desc : nullptr);
		return;
	}

	if (layer > 0) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = texture.dxgi_format;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srv_desc.Texture2DArray.MipLevels = mip_count;
		srv_desc.Texture2DArray.MostDetailedMip = mip;
		srv_desc.Texture2DArray.ArraySize = 1;
		srv_desc.Texture2DArray.FirstArraySlice = layer;
		srv_desc.Texture2DArray.ResourceMinLODClamp = 0;
		srv_desc.Texture2DArray.PlaneSlice = 0;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.Format = srv_desc.Format;
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uav_desc.Texture2DArray.ArraySize = 1;
		uav_desc.Texture2DArray.MipSlice = mip;
		uav_desc.Texture2DArray.PlaneSlice = 0;
		uav_desc.Texture2DArray.FirstArraySlice = layer;

		d3d->srv_heap.alloc(d3d->device, view.heap_id, texture.resource, srv_desc, compute_write ? &uav_desc : nullptr);
		return;
	}
	
	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
	srv_desc.Format = texture.dxgi_format;
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv_desc.Texture2D.MipLevels = mip_count;
	srv_desc.Texture2D.MostDetailedMip = mip;
	srv_desc.Texture2D.ResourceMinLODClamp = 0;
	srv_desc.Texture2D.PlaneSlice = 0;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
	uav_desc.Format = srv_desc.Format;
	uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uav_desc.Texture2D.MipSlice = mip;
	uav_desc.Texture2D.PlaneSlice = 0;

	d3d->srv_heap.alloc(d3d->device, view.heap_id, texture.resource, srv_desc, compute_write ? &uav_desc : nullptr);
}

void createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, TextureFlags flags, const char* debug_name) {
	ASSERT(handle);

	const bool is_srgb = u32(flags & TextureFlags::SRGB);
	const bool no_mips = u32(flags & TextureFlags::NO_MIPS);
	const bool readback = u32(flags & TextureFlags::READBACK);
	const bool is_3d = u32(flags & TextureFlags::IS_3D);
	const bool is_cubemap = u32(flags & TextureFlags::IS_CUBE);
	const bool compute_write = u32(flags & TextureFlags::COMPUTE_WRITE);
	const bool render_target = u32(flags & TextureFlags::RENDER_TARGET);

	switch (format) {
		case TextureFormat::R8:
		case TextureFormat::BGRA8:
		case TextureFormat::RGBA8:
		case TextureFormat::RGBA32F:
		case TextureFormat::R32F:
		case TextureFormat::RG32F:
		case TextureFormat::RG16F:
		case TextureFormat::RGB32F:
		case TextureFormat::SRGB:
		case TextureFormat::SRGBA:
		case TextureFormat::BC1:
		case TextureFormat::BC2:
		case TextureFormat::BC3:
		case TextureFormat::BC4:
		case TextureFormat::BC5: break;

		case TextureFormat::RG8:
		case TextureFormat::R16:
		case TextureFormat::RG16:
		case TextureFormat::RGBA16:
		case TextureFormat::R16F:
		case TextureFormat::RGBA16F:
		case TextureFormat::R11G11B10F:
		case TextureFormat::D32:
		case TextureFormat::D24S8: ASSERT(no_mips); break;
		default: ASSERT(false); return;
	}

	const u32 mip_count = no_mips ? 1 : 1 + log2(maximum(w, h, depth));
	Texture& texture = *handle;

	D3D12_HEAP_PROPERTIES props = {};
	props.Type = D3D12_HEAP_TYPE_DEFAULT;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = is_3d ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = depth * (is_cubemap ? 6 : 1);
	desc.MipLevels = mip_count;
	desc.Format = getDXGIFormat(format, is_srgb);
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = render_target ? (isDepthFormat(desc.Format) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) : D3D12_RESOURCE_FLAG_NONE;
	if (compute_write) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_CLEAR_VALUE clear_val = {};
	D3D12_CLEAR_VALUE* clear_val_ptr = nullptr;
	if (render_target) {
		clear_val_ptr = &clear_val;
		if (isDepthFormat(desc.Format)) {
			clear_val.Format = toDSViewFormat(desc.Format);
			clear_val.DepthStencil.Depth = 0.0f;
			clear_val.DepthStencil.Stencil = 0;
		} else {
			clear_val.Format = toViewFormat(desc.Format);
			clear_val_ptr = &clear_val;
			clear_val.Color[0] = 0.0f;
			clear_val.Color[1] = 0.0f;
			clear_val.Color[2] = 0.0f;
			clear_val.Color[3] = 1.0f;
		}
	}

	texture.state = isDepthFormat(desc.Format) ? D3D12_RESOURCE_STATE_COMMON : (compute_write ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_GENERIC_READ);
	if (d3d->device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, texture.state, clear_val_ptr, IID_PPV_ARGS(&texture.resource)) != S_OK) return;

	#ifdef LUMIX_DEBUG
		texture.name = debug_name;
	#endif

	texture.is_view = false;
	texture.flags = flags;
	texture.w = w;
	texture.h = h;
	texture.dxgi_format = desc.Format;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
	D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
	srv_desc.Format = toViewFormat(desc.Format);
	uav_desc.Format = srv_desc.Format;
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if (is_3d) {
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		srv_desc.Texture3D.MipLevels = mip_count;
		srv_desc.Texture3D.MostDetailedMip = 0;
		srv_desc.Texture3D.ResourceMinLODClamp = 0;

		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		uav_desc.Texture3D.MipSlice = 0;
		uav_desc.Texture3D.FirstWSlice = 0;
		uav_desc.Texture3D.WSize = -1;
	} else if (is_cubemap && depth <= 1) {
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv_desc.TextureCube.MipLevels = mip_count;
		srv_desc.TextureCube.MostDetailedMip = 0;
		srv_desc.TextureCube.ResourceMinLODClamp = 0;

		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav_desc.Texture2D.MipSlice = 0;
		uav_desc.Texture2D.PlaneSlice = 0;
	} else if (is_cubemap && depth > 1) {
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		srv_desc.TextureCubeArray.MipLevels = mip_count;
		srv_desc.TextureCubeArray.MostDetailedMip = 0;
		srv_desc.TextureCubeArray.ResourceMinLODClamp = 0;
		srv_desc.TextureCubeArray.First2DArrayFace = 0;
		srv_desc.TextureCubeArray.NumCubes = depth;

		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav_desc.Texture2D.MipSlice = 0;
		uav_desc.Texture2D.PlaneSlice = 0;
	} else if (depth > 1) {
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srv_desc.Texture2DArray.MipLevels = mip_count;
		srv_desc.Texture2DArray.MipLevels = mip_count;
		srv_desc.Texture2DArray.MostDetailedMip = 0;
		srv_desc.Texture2DArray.ResourceMinLODClamp = 0;
		srv_desc.Texture2DArray.PlaneSlice = 0;
		srv_desc.Texture2DArray.ArraySize = depth;

		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uav_desc.Texture2DArray.MipSlice = 0;
		uav_desc.Texture2DArray.PlaneSlice = 0;
		uav_desc.Texture2DArray.ArraySize = depth;
	}
	else {
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = mip_count;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.ResourceMinLODClamp = 0;
		srv_desc.Texture2D.PlaneSlice = 0;

		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav_desc.Texture2D.MipSlice = 0;
		uav_desc.Texture2D.PlaneSlice = 0;
	}

	d3d->srv_heap.alloc(d3d->device, texture.heap_id, texture.resource, srv_desc, compute_write ? &uav_desc : nullptr);

	if (debug_name) {
		WCHAR tmp[MAX_PATH];
		toWChar(tmp, debug_name);
		texture.resource->SetName(tmp);
	}
}

void setDebugName(TextureHandle texture, const char* debug_name) {
	WCHAR tmp[MAX_PATH];
	toWChar(tmp, debug_name);
	texture->resource->SetName(tmp);
}

IAllocator& getAllocator() { return d3d->allocator; }

void viewport(u32 x, u32 y, u32 w, u32 h) {
	D3D12_VIEWPORT vp = {};
	vp.Width = (float)w;
	vp.Height = (float)h;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = (float)x;
	vp.TopLeftY = (float)y;
	d3d->cmd_list->RSSetViewports(1, &vp);
	D3D12_RECT scissor;
	scissor.left = x;
	scissor.top = y;
	scissor.right = x + w;
	scissor.bottom = y + h;
	d3d->cmd_list->RSSetScissorRects(1, &scissor);
}

void requestDisassembly(ProgramHandle program) {
	if (!program->vs || !program->ps) return; // TODO
	ID3DBlob* vs_blob;
	ID3DBlob* ps_blob;
	HRESULT hr = D3DDisassemble(program->vs->GetBufferPointer(), program->vs->GetBufferSize(), 0, NULL, &vs_blob);
	ASSERT(hr == S_OK);
	hr = D3DDisassemble(program->ps->GetBufferPointer(), program->ps->GetBufferSize(), 0, NULL, &ps_blob);
	ASSERT(hr == S_OK);
	MutexGuard guard(d3d->disassembly_mutex);
	program->disassembly = "";
	program->disassembly.append("====VS====\n", StringView((const char*)vs_blob->GetBufferPointer(), (u32)vs_blob->GetBufferSize()));
	program->disassembly.append("====PS====\n", StringView((const char*)ps_blob->GetBufferPointer(), (u32)ps_blob->GetBufferSize()));
	vs_blob->Release();
	ps_blob->Release();
}

bool getDisassembly(ProgramHandle program, String& output) {
	MutexGuard guard(d3d->disassembly_mutex);
	if (program->disassembly.length() == 0) return false;
	output = program->disassembly;
	return true;
}


void useProgram(ProgramHandle handle) {
	if (handle != d3d->current_program) {
		d3d->pso_cache.last = nullptr;
		d3d->current_program = handle;
	}
}

void scissor(u32 x, u32 y, u32 w, u32 h) {
	D3D12_RECT rect;
	rect.left = x;
	rect.top = y;
	rect.right = x + w;
	rect.bottom = y + h;
	d3d->cmd_list->RSSetScissorRects(1, &rect);
}

enum class PipelineType {
	NONE,
	COMPUTE,
	GRAPHICS
};

static PipelineType g_last_pipeline_type = PipelineType::NONE;
static ProgramHandle g_last_program = INVALID_PROGRAM;

static void applyGFXUniformBlocks() {
	if (d3d->dirty_gfx_uniform_blocks == 0) return;
	for (u32 i = 0; i < 6; ++i) {
		if (d3d->dirty_gfx_uniform_blocks & (1 << i)) {
			d3d->cmd_list->SetGraphicsRootConstantBufferView(i, d3d->uniform_blocks[i]);
		}
	}
	d3d->dirty_gfx_uniform_blocks = 0;
}

static void applyComputeUniformBlocks() {
	if (d3d->dirty_compute_uniform_blocks == 0) return;
	for (u32 i = 0; i < 6; ++i) {
		if (d3d->dirty_compute_uniform_blocks & (1 << i)) {
			d3d->cmd_list->SetComputeRootConstantBufferView(i, d3d->uniform_blocks[i]);
		}
	}
	d3d->dirty_compute_uniform_blocks = 0;
}

[[nodiscard]] static bool setPipelineStateCompute() {
	if (g_last_pipeline_type != PipelineType::COMPUTE || g_last_program != d3d->current_program) {
		ID3D12PipelineState* pso = d3d->pso_cache.getPipelineStateCompute(d3d->device, d3d->root_signature, d3d->current_program);
		#ifdef LUMIX_DEBUG
			if (!pso) return false;
		#endif
		d3d->cmd_list->SetPipelineState(pso);
		g_last_pipeline_type = PipelineType::COMPUTE;
		g_last_program = d3d->current_program;
		d3d->cmd_list->SetComputeRootDescriptorTable(BINDLESS_SRV_ROOT_PARAMETER_INDEX, d3d->srv_heap.gpu_begin);
		d3d->cmd_list->SetComputeRootDescriptorTable(BINDLESS_SAMPLERS_ROOT_PARAMETER_INDEX, d3d->sampler_heap.gpu_begin);
		if (d3d->bound_shader_buffers.ptr) d3d->cmd_list->SetComputeRootDescriptorTable(SRV_ROOT_PARAMETER_INDEX, d3d->bound_shader_buffers);
	}
	applyComputeUniformBlocks();
	return true;
}

[[nodiscard]] static bool setPipelineStateGraphics() {
	const u8 stencil_ref = u8(u64(d3d->current_program->state) >> 34);
	d3d->cmd_list->OMSetStencilRef(stencil_ref);

	ID3D12PipelineState* pso = d3d->pso_cache.getPipelineState(d3d->device, d3d->current_program, d3d->current_framebuffer, d3d->root_signature);
	#ifdef LUMIX_DEBUG
		if (!pso) return false;
	#endif
	d3d->cmd_list->SetPipelineState(pso);
	g_last_pipeline_type = PipelineType::GRAPHICS;
	d3d->cmd_list->SetGraphicsRootDescriptorTable(BINDLESS_SRV_ROOT_PARAMETER_INDEX, d3d->srv_heap.gpu_begin);
	d3d->cmd_list->SetGraphicsRootDescriptorTable(BINDLESS_SAMPLERS_ROOT_PARAMETER_INDEX, d3d->sampler_heap.gpu_begin);
	if (d3d->bound_shader_buffers.ptr) d3d->cmd_list->SetGraphicsRootDescriptorTable(SRV_ROOT_PARAMETER_INDEX, d3d->bound_shader_buffers);
	applyGFXUniformBlocks();
	return true;
}

void drawArraysInstanced(u32 indices_count, u32 instances_count) {
	ASSERT(d3d->current_program);
	if (setPipelineStateGraphics()) {
		d3d->cmd_list->IASetPrimitiveTopology(d3d->current_program->primitive_topology);
		d3d->cmd_list->DrawInstanced(indices_count, instances_count, 0, 0);
	}
}

void drawArrays(u32 offset, u32 count) {
	ASSERT(d3d->current_program);
	if (setPipelineStateGraphics()) {
		d3d->cmd_list->IASetPrimitiveTopology(d3d->current_program->primitive_topology);
		d3d->cmd_list->DrawInstanced(count, 1, offset, 0);
	}
}

bool isOriginBottomLeft() {
	return false;
}

void destroy(BufferHandle buffer) {
	checkThread();
	ASSERT(buffer);
	Buffer& t = *buffer;
	if (t.resource) d3d->frame->to_release.push(t.resource);
	if (t.heap_id != INVALID_HEAP_ID) d3d->frame->to_heap_release.push(t.heap_id);

	LUMIX_DELETE(d3d->allocator, buffer);
}


void bindShaderBuffers(Span<BufferHandle> buffers) {
	ID3D12Resource* resources[16];
	D3D12_SHADER_RESOURCE_VIEW_DESC descs[16] = {};

	ASSERT(buffers.length() <= lengthOf(resources));
	for(u32 i = 0; i < buffers.length(); ++i) {
		resources[i] = buffers[i] ? buffers[i]->resource : nullptr;
		if (buffers[i]) {
			descs[i].ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			descs[i].Format = DXGI_FORMAT_R32_UINT;
			descs[i].Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			descs[i].Buffer.FirstElement = 0;
			descs[i].Buffer.NumElements = UINT(buffers[i]->size / sizeof(u32));
			descs[i].Buffer.StructureByteStride = 0;
			descs[i].Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			buffers[i]->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}
	d3d->bound_shader_buffers = d3d->srv_heap.allocTransient(d3d->device, Span(resources, buffers.length()), Span(descs, buffers.length()));
}

void bindUniformBuffer(u32 index, BufferHandle buffer, size_t offset, size_t size) {
	ASSERT(index < lengthOf(d3d->uniform_blocks));
	if (buffer) {
		ID3D12Resource* b = buffer->resource;
		ASSERT(b);
		d3d->uniform_blocks[index] = b->GetGPUVirtualAddress() + offset;
	} else {
		D3D12_GPU_VIRTUAL_ADDRESS dummy = {};
		d3d->uniform_blocks[index] = dummy;
	}
	d3d->dirty_compute_uniform_blocks |= 1 << index;
	d3d->dirty_gfx_uniform_blocks |= 1 << index;
}


void bindIndirectBuffer(BufferHandle handle) {
	d3d->current_indirect_buffer = handle;
	if (handle) handle->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
}

void bindIndexBuffer(BufferHandle handle) {
	d3d->current_index_buffer = handle;
}

void dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z) {
	ASSERT(d3d->current_program);
	if (setPipelineStateCompute()) {
		d3d->cmd_list->Dispatch(num_groups_x, num_groups_y, num_groups_z);
	}
}

void bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride_in_bytes) {
	if (buffer) {
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = buffer->resource->GetGPUVirtualAddress() + buffer_offset;
		vbv.StrideInBytes = stride_in_bytes;
		vbv.SizeInBytes = UINT(buffer->size - buffer_offset);
		d3d->cmd_list->IASetVertexBuffers(binding_idx, 1, &vbv);
	} else {
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = 0;
		vbv.StrideInBytes = stride_in_bytes;
		vbv.SizeInBytes = 0;
		d3d->cmd_list->IASetVertexBuffers(binding_idx, 1, &vbv);
	}
}

BindlessHandle getBindlessHandle(TextureHandle texture) {
	return BindlessHandle(texture->heap_id);
}

BindlessHandle getBindlessHandle(BufferHandle buffer) {
	return BindlessHandle(buffer->heap_id);
}

RWBindlessHandle getRWBindlessHandle(TextureHandle texture) {
	return RWBindlessHandle(texture->heap_id + 1);
}

RWBindlessHandle getRWBindlessHandle(BufferHandle buffer) {
	return RWBindlessHandle(buffer->heap_id + 1);
}

void drawIndirect(DataType index_type, u32 indirect_buffer_offset) {
	ASSERT(d3d->current_program);
	if (!setPipelineStateGraphics()) return;

	DXGI_FORMAT dxgi_index_type;
	u32 offset_shift = 0;
	switch (index_type) {
		case DataType::U32:
			dxgi_index_type = DXGI_FORMAT_R32_UINT;
			offset_shift = 2;
			break;
		case DataType::U16:
			dxgi_index_type = DXGI_FORMAT_R16_UINT;
			offset_shift = 1;
			break;
	}

	ASSERT(d3d->current_index_buffer);
	ID3D12Resource* b = d3d->current_index_buffer->resource;
	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = b->GetGPUVirtualAddress();
	ibv.Format = dxgi_index_type;
	ibv.SizeInBytes = d3d->current_index_buffer->size;
	d3d->cmd_list->IASetIndexBuffer(&ibv);
	d3d->cmd_list->IASetPrimitiveTopology(d3d->current_program->primitive_topology);

	static ID3D12CommandSignature* signature = [&]() {
		D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
		arg_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

		D3D12_COMMAND_SIGNATURE_DESC desc = {};
		desc.NodeMask = 1;
		desc.ByteStride = sizeof(u32) * 5;
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs = &arg_desc;
		ID3D12CommandSignature* result;
		d3d->device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&result));
		return result;
	}();

	d3d->cmd_list->ExecuteIndirect(signature, 1, d3d->current_indirect_buffer->resource, indirect_buffer_offset, nullptr, 0);
}

void drawIndexedInstanced(u32 indices_count, u32 instances_count, DataType index_type) {

	ASSERT(d3d->current_program);
	if (!setPipelineStateGraphics()) return;

	DXGI_FORMAT dxgi_index_type;
	u32 offset_shift = 0;
	switch (index_type) {
		case DataType::U32:
			dxgi_index_type = DXGI_FORMAT_R32_UINT;
			offset_shift = 2;
			break;
		case DataType::U16:
			dxgi_index_type = DXGI_FORMAT_R16_UINT;
			offset_shift = 1;
			break;
	}

	ASSERT(d3d->current_index_buffer);
	ID3D12Resource* b = d3d->current_index_buffer->resource;
	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = b->GetGPUVirtualAddress();
	ibv.Format = dxgi_index_type;
	ibv.SizeInBytes = indices_count * (1 << offset_shift);
	d3d->cmd_list->IASetIndexBuffer(&ibv);
	d3d->cmd_list->IASetPrimitiveTopology(d3d->current_program->primitive_topology);
	d3d->cmd_list->DrawIndexedInstanced(indices_count, instances_count, 0, 0, 0);
}

void drawIndexed(u32 offset_bytes, u32 count, DataType index_type) {
	if (!setPipelineStateGraphics()) return;

	DXGI_FORMAT dxgi_index_type;
	u32 offset_shift = 0;
	switch (index_type) {
		case DataType::U32:
			dxgi_index_type = DXGI_FORMAT_R32_UINT;
			offset_shift = 2;
			break;
		case DataType::U16:
			dxgi_index_type = DXGI_FORMAT_R16_UINT;
			offset_shift = 1;
			break;
	}

	ASSERT((offset_bytes & (offset_shift - 1)) == 0);
	ASSERT(d3d->current_index_buffer);
	ID3D12Resource* b = d3d->current_index_buffer->resource;
	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = b->GetGPUVirtualAddress() + offset_bytes;
	ibv.Format = dxgi_index_type;
	ibv.SizeInBytes = count * (1 << offset_shift);
	d3d->cmd_list->IASetIndexBuffer(&ibv);
	d3d->cmd_list->IASetPrimitiveTopology(d3d->current_program->primitive_topology);
	d3d->cmd_list->DrawIndexedInstanced(count, 1, 0, 0, 0);
}

void copy(BufferHandle dst, BufferHandle src, u32 dst_offset, u32 src_offset, u32 size) {
	ASSERT(src);
	ASSERT(dst);
	ASSERT(!dst->mapped_ptr);
	ASSERT(!src->mapped_ptr);
	D3D12_RESOURCE_STATES prev_dst = dst->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_DEST);
	D3D12_RESOURCE_STATES prev_src = src->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_GENERIC_READ);
	d3d->cmd_list->CopyBufferRegion(dst->resource, dst_offset, src->resource, src_offset, size);
	dst->setState(d3d->cmd_list, prev_dst);
	src->setState(d3d->cmd_list, prev_src);
}

void update(BufferHandle buffer, const void* data, size_t size) {
	checkThread();
	ASSERT(buffer);

	u8* dst = d3d->frame->scratch_buffer_ptr;
	ASSERT(size + dst <= d3d->frame->scratch_buffer_begin + SCRATCH_BUFFER_SIZE);
	memcpy(dst, data, size);
	UINT64 src_offset = dst - d3d->frame->scratch_buffer_begin;
	D3D12_RESOURCE_STATES prev_state = buffer->setState(d3d->cmd_list, D3D12_RESOURCE_STATE_COPY_DEST);
	d3d->cmd_list->CopyBufferRegion(buffer->resource, 0, d3d->frame->scratch_buffer, src_offset, size);
	buffer->setState(d3d->cmd_list, prev_state);

	d3d->frame->scratch_buffer_ptr += size;
}

void createProgram(ProgramHandle program
	, StateFlags state
	, const VertexDecl& decl
	, const char* src
	, ShaderType type
	, const char* name)
{
	ASSERT(program);
	#ifdef LUMIX_DEBUG
		program->name = name;
	#endif
	program->state = state;
	
	switch (decl.primitive_type) {
		case PrimitiveType::NONE:
		case PrimitiveType::TRIANGLES:
			program->primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			program->primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			break;
		case PrimitiveType::TRIANGLE_STRIP:
			program->primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			program->primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			break;
		case PrimitiveType::LINES:
			program->primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			program->primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			break;
		case PrimitiveType::POINTS:
			program->primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			program->primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
			break;
		default: ASSERT(0); break;
	}

	d3d->shader_compiler.compile(decl, src, type, name, *program);
}

} // namespace
