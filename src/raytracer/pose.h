#pragma once

#include "lumix.h"
#include "core/vec.h"
#include "core/quat.h"


namespace Lumix
{

class Model;


class LUMIX_RENDERER_API Pose
{
public:
	explicit Pose() {}

	void setMatrix(Matrix& mtx) const;
	const Vec3& getPosition() const { return m_position; }
	const Quat& getRotation() const { return m_rotation; }
	void blend(Pose& rhs, float weight);

private:
	Pose(const Pose&) = delete;
	void operator =(const Pose&) = delete;

private:
	Vec3 m_position;
	Quat m_rotation;
};


} // !namespace Lumix
