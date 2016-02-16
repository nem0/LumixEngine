#pragma once
#include "core/array.h"
#include "core/resource.h"
#include <bgfx/bgfx.h>


struct lua_State;


namespace Lumix
{

namespace FS
{
class IFile;
}

class Renderer;
class Shader;
class ShaderBinary;


class ShaderInstance
{
public:
	explicit ShaderInstance(Shader& shader)
		: m_shader(shader)
	{
		for (int i = 0; i < lengthOf(m_program_handles); ++i)
		{
			m_program_handles[i] = BGFX_INVALID_HANDLE;
			m_binaries[i * 2] = nullptr;
			m_binaries[i * 2 + 1] = nullptr;
		}
	}
	~ShaderInstance();

	bgfx::ProgramHandle m_program_handles[16];
	ShaderBinary* m_binaries[32];
	uint32 m_define_mask;
	Shader& m_shader;
};


class LUMIX_RENDERER_API ShaderCombinations
{
public:
	ShaderCombinations();

	void parse(Renderer& renderer, lua_State* state);
	void parsePasses(lua_State* state);
	void parseCombinations(Renderer& renderer,
						   lua_State* L,
						   const char* name,
						   int* output);

public:
	typedef char Define[40];
	typedef char Pass[20];
	typedef uint8 Defines[16];
	typedef Pass Passes[16];

	int m_pass_count;
	int m_define_count;
	int m_vs_local_mask[16];
	int m_fs_local_mask[16];
	Defines m_defines;
	Passes m_passes;
	uint32 m_all_defines_mask;
};


class LUMIX_RENDERER_API ShaderBinary : public Resource
{
public:
	ShaderBinary(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	bgfx::ShaderHandle getHandle() { return m_handle; }

private:
	void unload() override;
	bool load(FS::IFile& file) override;

private:
	bgfx::ShaderHandle m_handle;
};


class LUMIX_RENDERER_API Shader : public Resource
{
	friend class ShaderInstance;

public:
	struct TextureSlot
	{
		TextureSlot() { reset(); }

		void reset() { m_name[0] = m_uniform[0] = '\0'; m_define_idx = -1; m_is_atlas = false; }

		char m_name[30];
		char m_uniform[30];
		int m_define_idx;
		bool m_is_atlas;
		uint32 m_uniform_hash;
		bgfx::UniformHandle m_uniform_handle;
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
			VEC3
		};

		char name[32];
		uint32 name_hash;
		Type type;
		bgfx::UniformHandle handle;
	};


public:
	static const int MAX_TEXTURE_SLOT_COUNT = 16;

public:
	Shader(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Shader();

	bool hasDefine(uint8 define_idx) const;
	ShaderInstance& getInstance(uint32 mask);
	ShaderInstance* getFirstInstance();
	const TextureSlot& getTextureSlot(int index) const { return m_texture_slots[index]; }
	int getTextureSlotCount() const { return m_texture_slot_count; }
	Renderer& getRenderer();
	Uniform& getUniform(int index) { return m_uniforms[index]; }
	int getUniformCount() const { return m_uniforms.size(); }
	uint64 getRenderStates() const { return m_render_states; }

	static bool getShaderCombinations(Renderer& renderer,
		const char* shader_content,
		ShaderCombinations* output);

private:
	void parseTextureSlots(lua_State* state);
	void parseUniforms(lua_State* state);
	void clearUniforms();
	bool generateInstances();
	uint32 getDefineMaskFromDense(uint32 dense) const;

	void onBeforeReady() override;
	void unload(void) override;
	bool load(FS::IFile& file) override;

private:
	IAllocator& m_allocator;
	TextureSlot m_texture_slots[MAX_TEXTURE_SLOT_COUNT];
	int m_texture_slot_count;
	Array<ShaderInstance*> m_instances;
	Array<Uniform> m_uniforms;
	ShaderCombinations m_combintions;
	uint64 m_render_states;
};

} // ~namespace Lumix
