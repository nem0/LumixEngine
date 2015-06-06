#pragma once
#include "core/aabb.h"
#include "core/array.h"
#include "core/delegate.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include <bgfx.h>


namespace Lumix
{


namespace FS
{
	class IFile;
}

class Renderer;
class Shader;


class Geometry
{
	public:
		typedef Delegate<void(void*, int, int)> VertexCallback;
		typedef Delegate<void(void*, int, int)> IndexCallback;

	public:
		Geometry();
		~Geometry();

		bgfx::VertexBufferHandle getAttributesArrayID() const { return m_attributes_array_id; }
		bgfx::IndexBufferHandle getIndicesArrayID() const { return m_indices_array_id; }

		void setAttributesData(const void* data, int size, const bgfx::VertexDecl& decl);
		void setIndicesData(const void* data, int size);
		void bindBuffers() const;
		void copy(const Geometry& source, int copy_count, IndexCallback index_callback, VertexCallback vertex_callback, IAllocator& allocator);
		void clear();

	private:
		bgfx::VertexBufferHandle m_attributes_array_id;
		bgfx::IndexBufferHandle m_indices_array_id;
		int m_indices_data_size;
		int m_attributes_data_size;
};


} // ~namespace Lumix

