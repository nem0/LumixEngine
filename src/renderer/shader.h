#pragma once
#include "core/array.h"
#include "core/resource.h"
#include <bgfx/bgfx.h>


struct lua_State;


namespace Lumix
{

namespace FS
{
class FileSystem;
class IFile;
}

struct Matrix;
struct Vec3;
class Renderer;


class ShaderInstance
{
public:
	ShaderInstance(IAllocator&)
	{
		for (int i = 0; i < lengthOf(m_program_handles); ++i)
		{
			m_program_handles[i] = BGFX_INVALID_HANDLE;
		}
	}
	~ShaderInstance();

	bgfx::ProgramHandle m_program_handles[16];
	uint32_t m_combination;
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
	typedef int Defines[16];
	typedef Pass Passes[16];

	int m_pass_count;
	int m_define_count;
	int m_vs_combinations[16];
	int m_fs_combinations[16];
	Defines m_defines;
	Passes m_passes;
	int m_define_idx_map[32];
};


class LUMIX_RENDERER_API Shader : public Resource
{
	friend struct ShaderLoader;

public:
	class TextureSlot
	{
	public:
		TextureSlot() { reset(); }

		void reset() { m_name[0] = m_uniform[0] = '\0'; m_define_idx = -1; }

		char m_name[30];
		char m_uniform[30];
		int m_define_idx;
		uint32_t m_uniform_hash;
		bgfx::UniformHandle m_uniform_handle;
	};


public:
	static const int MAX_TEXTURE_SLOT_COUNT = 16;

public:
	Shader(const Path& path,
		   ResourceManager& resource_manager,
		   IAllocator& allocator);
	~Shader();

	uint32_t getDefineMask(int define_idx) const;
	ShaderInstance& getInstance(uint32_t mask);
	const TextureSlot& getTextureSlot(int index) const
	{
		return m_texture_slots[index];
	}
	int getTextureSlotCount() const { return m_texture_slot_count; }
	Renderer& getRenderer();

	static bool getShaderCombinations(Renderer& renderer,
									  const char* shader_content,
									  ShaderCombinations* output);

private:
	void parseTextureSlots(lua_State* state);
	bool generateInstances();

	virtual void doUnload(void) override;
	virtual void
	loaded(FS::IFile& file, bool success, FS::FileSystem& fs) override;

private:
	IAllocator& m_allocator;
	TextureSlot m_texture_slots[MAX_TEXTURE_SLOT_COUNT];
	int m_texture_slot_count;
	Array<ShaderInstance*> m_instances;
	ShaderCombinations m_combintions;
};

} // ~namespace Lumix
