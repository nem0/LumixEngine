#pragma once
#include "core/array.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/tag_allocator.h"
#include "core/string.h"
#include "engine/resource.h"
#include "gpu/gpu.h"


namespace black
{


struct DrawStream;
struct Renderer;
struct Texture;

struct ShaderKey {
	bool operator==(const ShaderKey& rhs) const;
	gpu::StateFlags state;
	u32 defines;
	u32 decl_hash; // this does not need to match gpu::VertexDecl::hash
	const char* semantic_defines;
};

struct BLACK_RENDERER_API Shader final : Resource {
	struct Header {
		static const u32 MAGIC = '_SHD';
		u32 magic = MAGIC;
		u32 version = 0;
	};
	
	struct TextureSlot
	{
		TextureSlot()
		{
			name[0] = '\0';
			define_idx = -1;
			default_texture = nullptr;
		}

		char name[32];
		int define_idx = -1;
		Texture* default_texture = nullptr;
	};

	struct Uniform
	{
		enum Type : u8
		{
			INT,
			FLOAT,
			COLOR,
			FLOAT2,
			FLOAT3,
			FLOAT4,
			NORMALIZED_FLOAT
		};

		union
		{
			float float_value;
			float vec4[4];
			float vec3[3];
			float vec2[2];
		} default_value;

		char name[32];
		RuntimeHash name_hash;
		Type type;
		u32 offset;
		u32 size() const;
	};

	Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	bool hasDefine(u8 define) const;
	
	gpu::ProgramHandle getProgram(u32 defines);
	gpu::ProgramHandle getProgram(gpu::StateFlags state, const gpu::VertexDecl& decl, u32 defines, const char* attr_defines);
	gpu::ProgramHandle getProgram(gpu::StateFlags state, const gpu::VertexDecl& decl, const gpu::VertexDecl& decl2, u32 defines, const char* attr_defines);
	void compile(gpu::ProgramHandle program, const ShaderKey& key, gpu::VertexDecl decl, DrawStream& stream);
	static void toUniformVarName(Span<char> out, const char* in);
	static void toTextureVarName(Span<char> out, const char* in);

	TagAllocator m_allocator;
	Renderer& m_renderer;
	u32 m_all_defines_mask;
	TextureSlot m_texture_slots[16];
	u32 m_texture_slot_count;
	Array<Uniform> m_uniforms;
	Array<u8> m_defines;
	struct ProgramPair {
		ShaderKey key;
		gpu::ProgramHandle program;
	};
	Array<ProgramPair> m_programs;
	gpu::ShaderType m_type;
	String m_code;

	static const ResourceType TYPE;
	StableHash m_content_hash;

private:
	void unload() override;
	bool load(Span<const u8> mem) override;
	void onBeforeReady() override;
};

template<>
struct HashFunc<ShaderKey> {
	static u32 mix(u32 a, u32 b) {
		b *= 0x5bd1e995;
		b ^= b >> 24;
		a *= 0x5bd1e995;
		return a * 0x5bd1e995;
	}

	static u32 get(const ShaderKey& key) {
		return mix(HashFunc<u64>::get(((u64*)&key)[0]), HashFunc<u64>::get(((u64*)&key)[1]));
	}
};

} // namespace black
