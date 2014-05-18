#include "graphics/model_instance.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/model.h"

namespace Lux
{


ModelInstance::ModelInstance(Model& model)
	: m_model(model)
	, m_matrix(Matrix::IDENTITY)
{
	model.getObserverCb().bind<ModelInstance, &ModelInstance::modelUpdate>(this);
	m_pose.resize(model.getBoneCount());
	model.getPose(m_pose);
}


ModelInstance::~ModelInstance()
{
	m_model.getObserverCb().unbind<ModelInstance, &ModelInstance::modelUpdate>(this);
	m_model.getResourceManager().get(ResourceManager::MODEL)->unload(m_model);
}


void ModelInstance::modelUpdate(Resource::State new_state)
{
	if(new_state == Resource::State::READY)
	{
		m_pose.resize(m_model.getBoneCount());
		m_model.getPose(m_pose);
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


} // ~namespace Lux
