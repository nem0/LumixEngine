#include "renderer/pose.h"
#include "engine/core/matrix.h"
#include "engine/core/quat.h"
#include "engine/core/profiler.h"
#include "engine/core/vec.h"
#include "renderer/model.h"


namespace Lumix
{


Pose::Pose(IAllocator& allocator)
	: allocator(allocator)
{
	positions = nullptr;
	rotations = nullptr;
	count = 0;
	is_absolute = false;
}


Pose::~Pose()
{
	allocator.deallocate(positions);
	allocator.deallocate(rotations);
}


void Pose::blend(Pose& rhs, float weight)
{
	ASSERT(count == rhs.count);
	if (weight <= 0.001f) return;
	weight = Math::clamp(weight, 0.0f, 1.0f);
	float inv = 1.0f - weight;
	for (int i = 0, c = count; i < c; ++i)
	{
		positions[i] = positions[i] * inv + rhs.positions[i] * weight;
		nlerp(rotations[i], rhs.rotations[i], &rotations[i], weight);
	}
}


void Pose::resize(int count)
{
	is_absolute = false;
	allocator.deallocate(positions);
	allocator.deallocate(rotations);
	this->count = count;
	if (count)
	{
		positions = static_cast<Vec3*>(allocator.allocate(sizeof(Vec3) * count));
		rotations = static_cast<Quat*>(allocator.allocate(sizeof(Quat) * count));
	}
	else
	{
		positions = nullptr;
		rotations = nullptr;
	}
}


void Pose::computeAbsolute(Model& model)
{
	PROFILE_FUNCTION();
	if (is_absolute) return;
	for (int i = model.getFirstNonrootBoneIndex(); i < count; ++i)
	{
		int parent = model.getBone(i).parent_idx;
		positions[i] = rotations[parent] * positions[i] + positions[parent];
		rotations[i] = rotations[i] * rotations[parent];
	}
	is_absolute = true;
}


void Pose::computeRelative(Model& model)
{
	PROFILE_FUNCTION();
	if (!is_absolute) return;
	for (int i = count - 1; i >= model.getFirstNonrootBoneIndex(); --i)
	{
		int parent = model.getBone(i).parent_idx;
		positions[i] = -rotations[parent] * (positions[i] - positions[parent]);
		rotations[i] = rotations[i] * -rotations[parent];
	}
	is_absolute = false;
}


void Pose::setMatrices(Matrix* mtx) const
{
	for (int i = 0, c = count; i < c; ++i)
	{
		rotations[i].toMatrix(mtx[i]);
		mtx[i].translate(positions[i]);
	}
}


} // namespace Lumix
