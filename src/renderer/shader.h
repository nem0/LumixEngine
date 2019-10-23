#pragma once
#include "engine/array.h"
#include "engine/hash_map.h"
#include "engine/resource.h"
#include "ffr/ffr.h"
#include "renderer/model.h"


struct lua_State;


namespace Lumix
{


class Renderer;
class Texture;


class LUMIX_RENDERER_API Shader final : public Resource
{
public:
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
		enum Type
		{
			INT,
			FLOAT,
			MATRIX4,
			COLOR,
			VEC2,
			VEC3,
			VEC4
		};

		char name[32];
		u32 name_hash;
		Type type;
		u32 offset;
		u32 size() const;
	};

	struct Stage {
		Stage(IAllocator& allocator) : code(allocator) {}
		ffr::ShaderType type;
		Array<char> code;
	};

	struct Sources {
		Sources(IAllocator& allocator) 
			: stages(allocator)
			, common(allocator)
		{}

		Path path;
		Array<Shader::Stage> stages;
		String common;
	};

	Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Shader();

	ResourceType getType() const override { return TYPE; }
	bool hasDefine(u8 define) const;

	ffr::ProgramHandle getProgram(const ffr::VertexDecl& decl, u32 defines);
	static void compile(ffr::ProgramHandle program, ffr::VertexDecl decl, u32 defines, const Sources& sources, Renderer& renderer);

	IAllocator& m_allocator;
	Renderer& m_renderer;
	u32 m_all_defines_mask;
	TextureSlot m_texture_slots[16];
	u32 m_texture_slot_count;
	Array<Uniform> m_uniforms;
	Array<u8> m_defines;
	HashMap<u64, ffr::ProgramHandle> m_programs;
	Sources m_sources;

	static const ResourceType TYPE;

private:
	void unload() override;
	bool load(u64 size, const u8* mem) override;
};


} // namespace Lumix
