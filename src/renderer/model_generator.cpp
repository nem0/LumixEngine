#include "model_generator.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "renderer/model.h"


namespace Lumix
{


	ModelGenerator::ModelGenerator(ResourceManager& resource_manager, IAllocator& allocator)
		: m_resource_manager(resource_manager)
		, m_allocator(allocator)
		, m_model_index(0)
	{

	}


	void ModelGenerator::destroyModel(Model* model)
	{
		m_resource_manager.get(ResourceManager::MODEL)->unload(*model);
		m_resource_manager.get(ResourceManager::MODEL)->remove(model);
		LUMIX_DELETE(m_allocator, model);
	}


	Model* ModelGenerator::createModel(Material* material, const bgfx::VertexDecl& vertex_def, int* indices, int indices_size, unsigned char* attribute_array, int attributes_size)
	{
		char path[10];
		path[0] = '*';
		toCString(m_model_index, path + 1, sizeof(path) - 1);
		Model* model = LUMIX_NEW(m_allocator, Model)(Lumix::Path(path), m_resource_manager, m_allocator);
		model->create(vertex_def, material, indices, indices_size, attribute_array, attributes_size);
		m_resource_manager.get(ResourceManager::MODEL)->add(model);
		++m_model_index;
		return model;
	}

}