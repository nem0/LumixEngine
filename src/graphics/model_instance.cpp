#include "graphics/model_instance.h"
#include "graphics/model.h"


namespace Lux
{


ModelInstance::ModelInstance(Model& model)
	: m_model(model)
{
	model.onLoaded().bind<ModelInstance, &ModelInstance::modelLoaded>(this);
	m_matrix = Matrix::IDENTITY;
	m_pose.resize(model.getBoneCount());
	model.getPose(m_pose);
}


void ModelInstance::modelLoaded()
{
	m_pose.resize(m_model.getBoneCount());
	m_model.getPose(m_pose);
}


void ModelInstance::setMatrix(const Matrix& mtx)
{
	m_matrix = mtx;

}


} // ~namespace Lux
