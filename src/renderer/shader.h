#pragma once
#include "engine/array.h"
#include "engine/resource.h"
#include "engine/string.h"
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


struct ShaderInstance
{
	explicit ShaderInstance(Shader& _shader)
		: shader(_shader)
	{
		for (int i = 0; i < lengthOf(program_handles); ++i)
		{
			program_handles[i] = BGFX_INVALID_HANDLE;
			binaries[i * 2] = nullptr;
			binaries[i * 2 + 1] = nullptr;
		}
	}
	~ShaderInstance();
	bgfx::ProgramHandle getProgramHandle(int pass_idx);

	bgfx::ProgramHandle program_handles[32];
	ShaderBinary* binaries[64];
	uint32 define_mask;
	Shader& shader;
};


struct LUMIX_RENDERER_API ShaderCombinations
{
	ShaderCombinations();

	typedef char Define[40];
	typedef StaticString<20> Pass;
	typedef uint8 Defines[16];
	typedef Pass Passes[32];

	int pass_count;
	int define_count;
	int vs_local_mask[32];
	int fs_local_mask[32];
	Defines defines;
	Passes passes;
	uint32 all_defines_mask;
};


class LUMIX_RENDERER_API ShaderBinary : public Resource
{
public:
	ShaderBinary(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	bgfx::ShaderHandle getHandle() { return m_handle; }

private:
	void unload() override;
	bool load(FS::IFile& file) override;

private:
	bgfx::ShaderHandle m_handle;
};


class LUMIX_RENDERER_API Shader : public Resource
{
	friend struct ShaderInstance;

public:
	struct TextureSlot
	{
		TextureSlot()
		{
			name[0] = uniform[0] = '\0';
			define_idx = -1;
			uniform_handle = BGFX_INVALID_HANDLE;
		}

		char name[30];
		char uniform[30];
		int define_idx;
		bgfx::UniformHandle uniform_handle;
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
	Shader(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	~Shader();

	bool hasDefine(uint8 define_idx) const;
	ShaderInstance& getInstance(uint32 mask);
	Renderer& getRenderer();

	static bool getShaderCombinations(const char* shd_path,
		Renderer& renderer,
		const char* shader_content,
		ShaderCombinations* output);

	IAllocator& m_allocator;
	Array<ShaderInstance> m_instances;
	uint32 m_all_defines_mask;
	ShaderCombinations m_combintions;
	uint64 m_render_states;
	TextureSlot m_texture_slots[MAX_TEXTURE_SLOT_COUNT];
	int m_texture_slot_count;
	Array<Uniform> m_uniforms;

private:
	bool generateInstances();

	void unload(void) override;
	bool load(FS::IFile& file) override;
};

} // ~namespace Lumix
