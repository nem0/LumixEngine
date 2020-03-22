#include "renderer/pose.h"
#include "engine/math.h"
#include "engine/profiler.h"
#include "engine/math.h"
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
	weight = clamp(weight, 0.0f, 1.0f);
	float inv = 1.0f - weight;
	for (int i = 0, c = count; i < c; ++i)
	{
		positions[i] = positions[i] * inv + rhs.positions[i] * weight;
		rotations[i] = nlerp(rotations[i], rhs.rotations[i], weight);
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
	if (is_absolute) return;
	for (u32 i = model.getFirstNonrootBoneIndex(); i < count; ++i)
	{
		int parent = model.getBone(i).parent_idx;
		positions[i] = rotations[parent].rotate(positions[i]) + positions[parent];
		rotations[i] = rotations[parent] * rotations[i];
	}
	is_absolute = true;
}


void Pose::computeRelative(Model& model)
{
	if (!is_absolute) return;
	for (int i = count - 1; i >= model.getFirstNonrootBoneIndex(); --i)
	{
		int parent = model.getBone(i).parent_idx;
		positions[i] = rotations[parent].conjugated().rotate(positions[i] - positions[parent]);
		rotations[i] = rotations[parent].conjugated() * rotations[i];
	}
	is_absolute = false;
}


} // namespace Lumix
