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

struct Vec3;
struct Matrix;

class LUMIX_ENGINE_API Shader : public Resource
{
	public:
		enum class FixedCachedUniforms : int
		{
			WORLD_MATRIX,
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
			COUNT
		};
	
	public:
		static const int MAX_ATTRIBUTE_COUNT = 16;

	public:
		Shader(const Path& path, ResourceManager& resource_manager);
		~Shader();

		GLint getAttribId(int index) const { return m_vertex_attributes_ids[index]; }
		bool isShadowmapRequired() const { return m_is_shadowmap_required; }
		LUMIX_FORCE_INLINE GLint getUniformLocation(const char* name, uint32_t name_hash);
		GLuint getProgramId() const { return m_program_id; }
		GLint getFixedCachedUniformLocation(FixedCachedUniforms name) const { return m_fixed_cached_uniforms[(int)name]; }
		int getAttributeCount() const { return m_vertex_attribute_count; }

	private:
		GLuint attach(GLenum type, const char* src, int32_t length);
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
		bool deserializeSettings(class ISerializer& serializer, char* attributes[MAX_ATTRIBUTE_COUNT]);

		virtual void doUnload(void) override;
		virtual FS::ReadCallback getReadCallback() override;

	private:
		class CachedUniform
		{
			public:
				uint32_t m_name_hash;
				GLint m_location;
		};

	private:
		GLuint	m_program_id;
		GLuint	m_vertex_id;
		GLuint	m_fragment_id;
		GLint	m_vertex_attributes_ids[MAX_ATTRIBUTE_COUNT];
		int		m_vertex_attribute_count;
		bool	m_is_shadowmap_required;
		Array<CachedUniform> m_uniforms;
		GLint m_fixed_cached_uniforms[(int)FixedCachedUniforms::COUNT];
};


} // ~namespace Lumix
