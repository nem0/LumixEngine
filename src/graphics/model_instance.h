#pragma once


#include "core/matrix.h"
#include "core/resource.h"
#include "graphics/pose.h"


namespace Lux
{


class Model;


class ModelInstance
{
	public:
		ModelInstance(Model& model);
		~ModelInstance();

		Matrix& getMatrix() { return m_matrix; }
		Model& getModel() { return m_model; }
		Pose& getPose() { return m_pose; }
		void setMatrix(const Matrix& mtx);

	private:
		void modelUpdate(Resource::State old_state, Resource::State state);
		void operator=(const ModelInstance&);

	private:
		Pose		m_pose;
		Model&		m_model;
		Matrix		m_matrix;
};


} // ~namespace Lux
