#pragma once
#include "core/resource.h"
#include "graphics/gl_ext.h"
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
		static const int MAX_ATTRIBUTE_COUNT = 16;
		static const int MAX_TEXTURE_SLOT_COUNT = 16;

	public:
		Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
		~Shader();

		GLint getAttribId(int index) const { return m_current_combination->m_vertex_attributes_ids[index]; }
		LUMIX_FORCE_INLINE GLint getUniformLocation(const char* name, uint32_t name_hash);
		GLuint getProgramId() const { return m_current_combination->m_program_id; }
		int getAttributeCount() const { return m_attributes.size(); }
		void createCombination(const char* defines);
		void setCurrentCombination(uint32_t hash, uint32_t pass_hash) { m_current_combination = getCombination(hash, pass_hash); }
		bool hasPass(uint32_t pass_hash);
		const TextureSlot& getTextureSlot(int index) const { return m_texture_slots[index]; }
		int getTextureSlotCount() const { return m_texture_slot_count; }

	private:
		class CachedUniform
		{
			public:
				uint32_t m_name_hash;
				GLint m_location;
		};

		class Combination
		{
			public:
				Combination(IAllocator& allocator)
					: m_defines(allocator)
				{ }

				GLuint	m_program_id;
				GLuint	m_vertex_id;
				GLuint	m_fragment_id;
				uint32_t m_hash;
				uint32_t m_pass_hash;
				GLint	m_vertex_attributes_ids[MAX_ATTRIBUTE_COUNT];
				string m_defines;
		};

	private:
		void parseTextureSlots(lua_State* state);
		void parsePasses(lua_State* state);
		void parseAttributes(lua_State* state);
		void parseSourceCode(lua_State* state);
		Combination* getCombination(uint32_t hash, uint32_t pass_hash);

		virtual void doUnload(void) override;
		virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override;

	private:
		IAllocator&			m_allocator;
		TextureSlot			m_texture_slots[MAX_TEXTURE_SLOT_COUNT];
		int					m_texture_slot_count;
		Array<string>		m_attributes;
		Array<string>		m_passes;
		Array<uint32_t>		m_pass_hashes;
		Array<Combination*>	m_combinations;
		Combination*		m_current_combination;
		Combination			m_default_combination;
		string				m_vertex_shader_source;
		string				m_fragment_shader_source;
		Renderer&			m_renderer;
};


} // ~namespace Lumix
