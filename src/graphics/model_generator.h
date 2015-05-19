#pragma once


#include "core/lumix.h"


namespace Lumix
{
	
	class IAllocator;
	class Material;
	class Model;
	class ResourceManager;
	struct VertexDef;


	class LUMIX_ENGINE_API ModelGenerator
	{
		public:
			ModelGenerator(ResourceManager& resource_manager, IAllocator& allocator);

			Model* createModel(Material* material, const VertexDef& vertex_def, int* indices, int indices_size, unsigned char* attribute_array, int attributes_size);
			void destroyModel(Model* model);

		private:
			ResourceManager& m_resource_manager;
			IAllocator& m_allocator;
			int m_model_index;
	};


}