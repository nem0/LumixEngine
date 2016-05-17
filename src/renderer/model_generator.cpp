#include "model_generator.h"
#include "engine/crc32.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/model.h"


namespace Lumix
{

	static const uint32 MODEL_HASH = crc32("MODEL");


	ModelGenerator::ModelGenerator(ResourceManager& resource_manager, IAllocator& allocator)
		: m_resource_manager(resource_manager)
		, m_allocator(allocator)
		, m_model_index(0)
	{

	}


	void ModelGenerator::destroyModel(Model* model)
	{
		m_resource_manager.get(MODEL_HASH)->unload(*model);
		m_resource_manager.get(MODEL_HASH)->remove(model);
		LUMIX_DELETE(m_allocator, model);
	}


	Model* ModelGenerator::createModel(Material* material,
		const bgfx::VertexDecl& vertex_def,
		int* indices,
		int indices_size,
		unsigned char* attribute_array,
		int attributes_size)
	{
		char path[10];
		path[0] = '*';
		toCString(m_model_index, path + 1, sizeof(path) - 1);
		Model* model = LUMIX_NEW(m_allocator, Model)(Lumix::Path(path), m_resource_manager, m_allocator);
		model->create(vertex_def, material, indices, indices_size, attribute_array, attributes_size);
		m_resource_manager.get(MODEL_HASH)->add(model);
		++m_model_index;
		return model;
	}

}