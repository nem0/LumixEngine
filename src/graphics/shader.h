#pragma once
#include "core/resource.h"
#include "graphics/gl_ext.h"


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
		enum class FixedCachedUniforms : int
		{
			GRASS_MATRICES,
			MORPH_CONST,
			QUAD_SIZE,
			QUAD_MIN,
			AMBIENT_COLOR,
			AMBIENT_INTENSITY,
			DIFFUSE_COLOR,
			DIFFUSE_INTENSITY,
			FOG_COLOR,
			FOG_DENSITY,
			VIEW_MATRIX,
			PROJECTION_MATRIX,
			SHADOWMAP_SPLITS,
			SHADOW_MATRIX0,
			SHADOW_MATRIX1,
			SHADOW_MATRIX2,
			SHADOW_MATRIX3,

			WORLD_MATRIX, // keep this before count
			COUNT
		};
	
	public:
		static const int MAX_ATTRIBUTE_COUNT = 16;

	public:
		Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
		~Shader();

		GLint getAttribId(int index) const { return m_current_combination->m_vertex_attributes_ids[index]; }
		bool isShadowmapRequired() const { return m_is_shadowmap_required; }
		LUMIX_FORCE_INLINE GLint getUniformLocation(const char* name, uint32_t name_hash);
		GLuint getProgramId() const { return m_current_combination->m_program_id; }
		GLint getFixedCachedUniformLocation(FixedCachedUniforms name) const { return m_current_combination->m_fixed_cached_uniforms[(int)name]; }
		int getAttributeCount() const { return m_attributes.size(); }
		void createCombination(const char* defines);
		void setCurrentCombination(uint32_t hash, uint32_t pass_hash) { m_current_combination = getCombination(hash, pass_hash); }
		bool hasPass(uint32_t pass_hash);

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
				Array<CachedUniform> m_uniforms;
				GLint m_fixed_cached_uniforms[(int)FixedCachedUniforms::COUNT];
				string m_defines;
		};

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
		bool deserializeSettings(class ISerializer& serializer, char* attributes[MAX_ATTRIBUTE_COUNT]);
		Combination* getCombination(uint32_t hash, uint32_t pass_hash) const;

		virtual void doUnload(void) override;
		virtual FS::ReadCallback getReadCallback() override;

	private:
		IAllocator&			m_allocator;
		Array<string>		m_attributes;
		Array<string>		m_passes;
		Array<uint32_t>		m_pass_hashes;
		Array<Combination*>	m_combinations;
		Combination*		m_current_combination;
		bool				m_is_shadowmap_required;
		string				m_source;
		Renderer&			m_renderer;
};


} // ~namespace Lumix
