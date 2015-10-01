#pragma once


#include <bgfx/bgfx.h>


namespace Lumix
{


namespace FS
{
class IFile;
}


class Shader;


class Geometry
{
public:
	Geometry();
	~Geometry();

	bgfx::VertexBufferHandle getAttributesArrayID() const
	{
		return m_attributes_array_id;
	}
	bgfx::IndexBufferHandle getIndicesArrayID() const
	{
		return m_indices_array_id;
	}

	void
	setAttributesData(const void* data, int size, const bgfx::VertexDecl& decl);
	void setIndicesData(const int* data, int size);
	void setIndicesData(const short* data, int size);
	void bindBuffers() const;
	void clear();

private:
	bgfx::VertexBufferHandle m_attributes_array_id;
	bgfx::IndexBufferHandle m_indices_array_id;
	int m_indices_data_size;
	int m_attributes_data_size;
};


} // ~namespace Lumix
