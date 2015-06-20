#pragma once
#include "core/array.h"
#include "core/associative_array.h"
#include "core/resource.h"
#include "core/string.h"
#include <bgfx.h>


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
			for (int i = 0; i < sizeof(m_program_handles) / sizeof(m_program_handles[0]); ++i)
			{
				m_program_handles[i] = BGFX_INVALID_HANDLE;
			}
		}
		~ShaderInstance();

		bgfx::ProgramHandle m_program_handles[16];
		uint32_t m_combination;
};


class ShaderCombinations
{
	public:
		ShaderCombinations() { memset(this, 0, sizeof(*this)); }

		void parse(lua_State* state);
		void parsePasses(lua_State* state);
		void parseCombinations(lua_State* L, const char* name, int* output);

	public:
		typedef char Define[40];
		typedef char Pass[20];
		typedef Define Defines[16];
		typedef Pass Passes[16];

		int m_pass_count;
		int m_define_count;
		int m_vs_combinations[16];
		int m_fs_combinations[16];
		Defines m_defines;
		Passes m_passes;
};


class LUMIX_ENGINE_API Shader : public Resource
{
	public:
		class TextureSlot
		{
			public:
				TextureSlot()
				{
					reset();
				}

				void reset()
				{
					m_name[0] = m_uniform[0] = m_define[0] = '\0';
				}

				char m_name[30];
				char m_uniform[30];
				char m_define[30];
				uint32_t m_uniform_hash;
				bgfx::UniformHandle m_uniform_handle;
		};

		

	public:
		static const int MAX_TEXTURE_SLOT_COUNT = 16;

	public:
		Shader(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		~Shader();

		uint32_t getDefineMask(const char* define) const;
		ShaderInstance& getInstance(uint32_t mask);
		const TextureSlot& getTextureSlot(int index) const { return m_texture_slots[index]; }
		int getTextureSlotCount() const { return m_texture_slot_count; }

		static bool getShaderCombinations(const char* shader_content, ShaderCombinations* output);

	private:
		void parseTextureSlots(lua_State* state);
		bool generateInstances();
		Renderer& getRenderer();
		bgfx::ProgramHandle createProgram(int pass_idx, int mask) const;

		virtual void doUnload(void) override;
		virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override;

	private:
		IAllocator&			m_allocator;
		TextureSlot			m_texture_slots[MAX_TEXTURE_SLOT_COUNT];
		int					m_texture_slot_count;
		Array<ShaderInstance*>	m_instances;
		ShaderCombinations	m_combintions;
};


} // ~namespace Lumix
