#pragma once


#include "lumix.h"


namespace bgfx
{
	struct VertexDecl;
}


namespace Lumix
{
	
	class IAllocator;
	class Material;
	class Model;
	class ResourceManager;
	struct VertexDef;


	class LUMIX_RENDERER_API ModelGenerator
	{
		public:
			ModelGenerator(ResourceManager& resource_manager, IAllocator& allocator);

			Model* createModel(Material* material, const bgfx::VertexDecl& vertex_def, int* indices, int indices_size, unsigned char* attribute_array, int attributes_size);
			void destroyModel(Model* model);

		private:
			ResourceManager& m_resource_manager;
			IAllocator& m_allocator;
			int m_model_index;
	};


}