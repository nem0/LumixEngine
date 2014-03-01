#pragma once
#include <Windows.h>
#include <gl/GL.h>
#include "core/pod_array.h"
#include "core/vec3.h"


namespace Lux
{


class Shader;


struct VertexAttributeDef
{
	enum Type
	{
		FLOAT4,
		INT4,
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

	private:
		VertexAttributeDef::Type m_attributes[16];
		int m_attribute_count;
		int m_vertex_size;
};



class Geometry
{
	public:
		Geometry();
		~Geometry();

		void copy(const uint8_t* data, int size, VertexDef vertex_definition);
		void draw(int start, int count, Shader& shader);
		const PODArray<Vec3>& getVertices() const { return m_vertices; }
		float getBoundingRadius() const; 

	private:
		GLuint m_id;
		VertexDef m_vertex_definition;
		PODArray<Vec3> m_vertices;
};


} // ~namespace Lux

