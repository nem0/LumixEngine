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
struct IFile;
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
	u32 define_mask;
	Shader& shader;
};


struct LUMIX_RENDERER_API ShaderCombinations
{
	ShaderCombinations();

	typedef StaticString<32> Pass;
	typedef u8 Defines[16];
	typedef Pass Passes[32];

	int pass_count;
	int define_count;
	int vs_local_mask[32];
	int fs_local_mask[32];
	Defines defines;
	Passes passes;
	u32 all_defines_mask;
};


class LUMIX_RENDERER_API ShaderBinary LUMIX_FINAL : public Resource
{
public:
	ShaderBinary(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	
	ResourceType getType() const override { return TYPE; }

	bgfx::ShaderHandle getHandle() { return m_handle; }

	Shader* m_shader;

	static const ResourceType TYPE;

private:
	void unload() override;
	bool load(FS::IFile& file) override;

private:
	bgfx::ShaderHandle m_handle;
};


class LUMIX_RENDERER_API Shader LUMIX_FINAL : public Resource
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
			VEC2,
			VEC3
		};

		char name[32];
		u32 name_hash;
		Type type;
		bgfx::UniformHandle handle;
	};


public:
	static const int MAX_TEXTURE_SLOT_COUNT = 16;

public:
	Shader(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	~Shader();

	ResourceType getType() const override { return TYPE; }

	bool hasDefine(u8 define_idx) const;
	ShaderInstance& getInstance(u32 mask);
	Renderer& getRenderer();

	static bool getShaderCombinations(const char* shd_path,
		Renderer& renderer,
		const char* shader_content,
		ShaderCombinations* output);

	void onBeforeEmpty() override;

	IAllocator& m_allocator;
	Array<ShaderInstance> m_instances;
	u32 m_all_defines_mask;
	ShaderCombinations m_combintions;
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

} // ~namespace Lumix
