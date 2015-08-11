#include "graphics/model_instance.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/model.h"

namespace Lumix
{


ModelInstance::ModelInstance(IAllocator& allocator)
	: m_model(nullptr)
	, m_matrix(Matrix::IDENTITY)
	, m_pose(allocator)
{
}


ModelInstance::~ModelInstance()
{
	setModel(nullptr);
}


void ModelInstance::setModel(Model* model)
{
	if (m_model)
	{
		m_model->getObserverCb().unbind<ModelInstance, &ModelInstance::modelUpdate>(this);
		m_model->getResourceManager().get(ResourceManager::MODEL)->unload(*m_model);
	}
	m_model = model;
	if (m_model)
	{
		m_model->onLoaded<ModelInstance, &ModelInstance::modelUpdate>(this);
		m_pose.resize(m_model->getBoneCount());
		m_model->getPose(m_pose);
	}
}


void ModelInstance::modelUpdate(Resource::State, Resource::State new_state)
{
	if(new_state == Resource::State::READY)
	{
		m_pose.resize(m_model->getBoneCount());
		m_model->getPose(m_pose);
	}
	else if(new_state == Resource::State::UNLOADING)
	{
		m_pose.resize(0);
	}
}


void ModelInstance::setMatrix(const Matrix& mtx)
{
	m_matrix = mtx;
}


} // ~namespace Lumix
