#pragma once


#include "core/matrix.h"
#include "core/resource.h"
#include "renderer/pose.h"


namespace Lumix
{


class Model;


class ModelInstance
{
	public:
		ModelInstance(IAllocator& allocator);
		~ModelInstance();

		void setModel(Model* model);
		const Matrix& getMatrix() const { return m_matrix; }
		Matrix& getMatrix() { return m_matrix; }
		Model* getModel() const { return m_model; }
		Pose& getPose() { return m_pose; }
		const Pose& getPose() const { return m_pose; }
		void setMatrix(const Matrix& mtx);

	private:
		void modelUpdate(Resource::State old_state, Resource::State new_state);
		void operator=(const ModelInstance&);
		ModelInstance(const ModelInstance&);


	private:
		Pose		m_pose;
		Model*		m_model;
		Matrix		m_matrix;
};


} // ~namespace Lumix
