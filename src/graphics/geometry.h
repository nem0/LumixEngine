#pragma once
#include "core/aabb.h"
#include "core/array.h"
#include "core/delegate.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include "graphics/shader.h"

namespace Lumix
{


class Shader;


enum class VertexAttributeDef : uint32_t
{
	POSITION,
	FLOAT1,
	FLOAT2,
	FLOAT3,
	FLOAT4,
	INT1,
	INT2,
	INT3,
	INT4,
	SHORT2,
	SHORT4,
	BYTE4,
	NONE
};


struct VertexDef
{
	public:
		VertexDef() : m_attribute_count(0) {}
		void addAttribute(Renderer& renderer, const char* name, VertexAttributeDef type);
		bool parse(Renderer& renderer, FS::IFile* file);
		int getVertexSize() const { return m_vertex_size; }
		int getPositionOffset() const;
		void begin(Shader& shader, int start_offset) const;
		void end(Shader& shader) const;
		VertexAttributeDef getAttributeType(int i) const { return i < m_attribute_count ? m_attributes[i].m_type : VertexAttributeDef::NONE; }

	private:
		class Attribute
		{
			public:
				VertexAttributeDef m_type;
				int m_name_index;
		};

	private:
		Attribute m_attributes[Shader::MAX_ATTRIBUTE_COUNT];
		int m_attribute_count;
		int m_vertex_size;
};


class Geometry
{
	public:
		typedef Delegate<void(void*, int, int)> VertexCallback;
		typedef Delegate<void(void*, int, int)> IndexCallback;

	public:
		Geometry();
		~Geometry();

		GLuint getAttributesArrayID() const { return m_attributes_array_id; }
		GLuint getIndicesArrayID() const { return m_indices_array_id; }

		void setAttributesData(const void* data, int size);
		void setIndicesData(const void* data, int size);
		void bindBuffers() const;
		void copy(const Geometry& source, int copy_count, IndexCallback index_callback, VertexCallback vertex_callback, IAllocator& allocator);
		void clear();

	private:
		GLuint m_attributes_array_id;
		GLuint m_indices_array_id;
		int m_indices_data_size;
		int m_attributes_data_size;
};


} // ~namespace Lumix

