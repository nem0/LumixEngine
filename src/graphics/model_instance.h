#pragma once


#include "core/matrix.h"
#include "graphics/pose.h"


namespace Lux
{


class Model;


class ModelInstance
{
	public:
		ModelInstance(Model& model);

		Matrix& getMatrix() { return m_matrix; }
		Model& getModel() { return m_model; }
		Pose& getPose() { return m_pose; }
		void setMatrix(const Matrix& mtx);

	private:
		void modelUpdate(uint32_t state);

	private:
		Pose		m_pose;
		Model&		m_model;
		Matrix		m_matrix;
};


} // ~namespace Lux
