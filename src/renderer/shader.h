#pragma once
#include "engine/array.h"
#include "engine/hash_map.h"
#include "engine/resource.h"
#include "engine/string.h"
#include "ffr/ffr.h"
#include "renderer/model.h"


struct lua_State;


namespace Lumix
{

namespace FS
{
struct IFile;
}

class Renderer;
class Shader;
class Texture;


class LUMIX_RENDERER_API Shader final : public Resource
{
public:
	struct TextureSlot
	{
		TextureSlot()
		{
			name[0] = '\0';
			uniform[0] = '\0';
			define_idx = -1;
			uniform_handle = ffr::INVALID_UNIFORM;
			default_texture = nullptr;
		}

		char name[32];
		char uniform[32];
		ffr::UniformHandle uniform_handle;
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
			TIME,
			COLOR,
			VEC2,
			VEC3,
			VEC4
		};

		char name[32];
		u32 name_hash;
		Type type;
	// TODO
/*
		bgfx::UniformHandle handle;*/
	};

	struct Source {
		Source(IAllocator& allocator) : code(allocator) {}
		ffr::ShaderType type;
		Array<char> code;
	};

	struct Program {
		int attribute_by_semantics[16];
		bool use_semantics;
		ffr::ProgramHandle handle;
	};

	struct AttributeInfo {
		StaticString<32> name;
		Mesh::AttributeSemantic semantic;
	};

public:
	static const int MAX_TEXTURE_SLOT_COUNT = 16;
	struct RenderData {
		RenderData(Renderer& renderer, IAllocator& allocator) 
			: allocator(allocator)
			, renderer(renderer)
			, programs(allocator) 
			, include(allocator)
			, sources(allocator)
			, attributes(allocator)
		{}
		IAllocator& allocator;
		Renderer& renderer;
		HashMap<u32, Program> programs;
		Array<Source> sources;
		Array<u8> include;
		Array<AttributeInfo> attributes;
	}* m_render_data;

public:
	Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Shader();

	ResourceType getType() const override { return TYPE; }

	static const Program& getProgram(RenderData* rd, u32 defines);

	IAllocator& m_allocator;
	Renderer& m_renderer;
	u32 m_all_defines_mask;
	u64 m_render_states;
	TextureSlot m_texture_slots[MAX_TEXTURE_SLOT_COUNT];
	int m_texture_slot_count;
	Array<Uniform> m_uniforms;

	static const ResourceType TYPE;

private:
	bool generateInstances();

	void unload() override;
	bool load(FS::IFile& file) override;
};

} // namespace Lumix
