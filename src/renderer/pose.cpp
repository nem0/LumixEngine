#include "renderer/pose.h"
#include "core/math.h"
#include "core/profiler.h"
#include "core/math.h"
#include "core/simd_math.h"
#include "renderer/model.h"


namespace black
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
		positions = static_cast<Vec3*>(allocator.allocate(sizeof(Vec3) * (count + 1), 16)); // + 1 padding so we don't access data outside allocation with simd
		rotations = static_cast<Quat*>(allocator.allocate(sizeof(Quat) * count, 16));
	}
	else
	{
		positions = nullptr;
		rotations = nullptr;
	}
}


void Pose::computeAbsolute(Model& model) {
	if (is_absolute) return;

	for (u32 i = model.getFirstNonrootBoneIndex(); i < count; ++i) {
		i32 parent = model.getBoneParent(i);
		
		if (i % 4 == 0 && i + 4 <= count) {
			bool all_parents_valid = true;
			for (u32 j = 0; j < 4; ++j) {
				if ((u32)model.getBoneParent(i + j) >= i) {
					all_parents_valid = false;
					break;
				}
			}
			if (all_parents_valid) {
				SOAQuat parent_rot;
				SOAVec3 parent_pos;
				// load parents
				parent_rot.x = f4Load(&rotations[parent]);
				parent_pos.x = f4LoadUnaligned(&positions[parent]);

				parent = model.getBoneParent(i + 1);
				parent_rot.y = f4Load(&rotations[parent]);
				parent_pos.y = f4LoadUnaligned(&positions[parent]);

				parent = model.getBoneParent(i + 2);
				parent_rot.z = f4Load(&rotations[parent]);
				parent_pos.z = f4LoadUnaligned(&positions[parent]);

				parent = model.getBoneParent(i + 3);
				parent_rot.w = f4Load(&rotations[parent]);
				float4 pos_tmp = f4LoadUnaligned(&positions[parent]);

				f4Transpose(parent_pos.x, parent_pos.y, parent_pos.z, pos_tmp);
				f4Transpose(parent_rot.x, parent_rot.y, parent_rot.z, parent_rot.w);

				// load children
				SOAQuat rot;
				loadTranspose(rot, &rotations[i]);

				SOAVec3 pos;
				pos.x = f4LoadUnaligned(&positions[i]);
				pos.y = f4LoadUnaligned(&positions[i + 1]);
				pos.z = f4LoadUnaligned(&positions[i + 2]);
				pos_tmp = f4LoadUnaligned(&positions[i + 3]);
				f4Transpose(pos.x, pos.y, pos.z, pos_tmp);

				// compute
				pos = rotate(parent_rot, pos) + parent_pos;
				rot = parent_rot * rot;

				// store
				f4Transpose(pos.x, pos.y, pos.z, pos_tmp);
				f4StoreUnaligned(&positions[i], pos.x);
				f4StoreUnaligned(&positions[i + 1], pos.y);
				f4StoreUnaligned(&positions[i + 2], pos.z);
				positions[i + 3].x = f4GetX(pos_tmp);
				positions[i + 3].y = f4GetY(pos_tmp);
				positions[i + 3].z = f4GetZ(pos_tmp);
				
				transposeStore(rot, &rotations[i]);
				i += 3;
				continue;
			}
		}

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
		int parent = model.getBoneParent(i);
		positions[i] = rotations[parent].conjugated().rotate(positions[i] - positions[parent]);
		rotations[i] = rotations[parent].conjugated() * rotations[i];
	}
	is_absolute = false;
}


} // namespace black
