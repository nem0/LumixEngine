#pragma once
#include "core/array.h"
#include "core/delegate.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


class Shader;


struct VertexAttributeDef
{
	enum Type
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
};


struct VertexDef
{
	public:
		void parse(const char* data, int size);
		int getVertexSize() const { return m_vertex_size; }
		int getPositionOffset() const;
		void begin(Shader& shader);
		void end(Shader& shader);
		VertexAttributeDef::Type getAttributeType(int i) const { return i < m_attribute_count ? m_attributes[i] : VertexAttributeDef::NONE; }

	private:
		VertexAttributeDef::Type m_attributes[16];
		int m_attribute_count;
		int m_vertex_size;
};



class Geometry
{
	public:
		typedef Delegate<void(Array<uint8_t>&)> VertexCallback;
		typedef Delegate<void(Array<int>&)> IndexCallback;

	public:
		Geometry();
		~Geometry();

		void copy(const uint8_t* data, int size, const Array<int32_t>& indices, VertexDef vertex_definition);
		void copy(const Geometry& source, int times, VertexCallback& vertex_callback, IndexCallback& index_callback);
		void draw(int start, int count, Shader& shader);
		const Array<Vec3>& getVertices() const { return m_vertices; }
		const Array<int32_t>& getIndices() const { return m_indices; }
		float getBoundingRadius() const; 
		const VertexDef& getVertexDefinition() const { return m_vertex_definition; }

	private:
		GLuint m_id;
		GLuint m_indices_id;
		VertexDef m_vertex_definition;
		Array<Vec3> m_vertices;
		Array<int32_t> m_indices;
};


} // ~namespace Lumix

