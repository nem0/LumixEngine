#pragma once
#include "core/aabb.h"
#include "core/array.h"
#include "core/delegate.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


class Shader;


enum class VertexAttributeDef
{
	FLOAT4,
	FLOAT2,
	INT4,
	INT1,
	POSITION,
	NORMAL,
	TEXTURE_COORDS,
	NONE
};


struct VertexDef
{
	public:
		void parse(const char* data, int size);
		int getVertexSize() const { return m_vertex_size; }
		int getPositionOffset() const;
		void begin(Shader& shader, int start_offset) const;
		void end(Shader& shader) const;
		VertexAttributeDef getAttributeType(int i) const { return i < m_attribute_count ? m_attributes[i] : VertexAttributeDef::NONE; }

	private:
		VertexAttributeDef m_attributes[16];
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
		void copy(IAllocator& allocator, const Geometry& source, int copy_count, IndexCallback index_callback, VertexCallback vertex_callback);
		void clear();

	private:
		GLuint m_attributes_array_id;
		GLuint m_indices_array_id;
		int m_indices_data_size;
		int m_attributes_data_size;
};


} // ~namespace Lumix

