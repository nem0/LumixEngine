#include "graphics/model_instance.h"
#include "graphics/model.h"


namespace Lux
{


ModelInstance::ModelInstance(Model& model)
	: m_model(model)
{
	m_matrix = Matrix::IDENTITY;
	m_pose.resize(model.getBoneCount());
	model.getPose(m_pose);
}


} // ~namespace Lux
