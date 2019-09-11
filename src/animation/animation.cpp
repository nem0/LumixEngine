#include "animation/animation.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/profiler.h"
#include "engine/stream.h"
#include "engine/math.h"
#include "renderer/model.h"
#include "renderer/pose.h"


namespace Lumix
{


enum class Version
{
	FIRST = 0,
	COMPRESSION = 1,
	ROOT_MOTION,

	LAST
};


const ResourceType Animation::TYPE("animation");


Animation::Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_mem(allocator)
	, m_bones(allocator)
	, m_root_motion_bone_idx(-1)
{
}


void Animation::getRelativePose(Time time, Pose& pose, const Model& model, float weight, const BoneMask* mask) const
{
	ASSERT(!pose.is_absolute);

	if (!model.isReady()) return;

	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;

	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		ASSERT(anim_t_highres <= 0xffFF);
		const u16 anim_t = u16(anim_t_highres);
		
		for (Bone& bone : m_bones) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(bone.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(bone.name) == mask->bones.end()) continue;

			ASSERT(bone.pos_count > 1);
			u32 idx = 1;
			for (u32 c = bone.pos_count; idx < c; ++idx) {
				if (bone.pos_times[idx] > anim_t) break;
			}

			float t = float(anim_t - bone.pos_times[idx - 1]) / (bone.pos_times[idx] - bone.pos_times[idx - 1]);

			int model_bone_index = iter.value();
			Vec3 anim_pos = lerp(bone.pos[idx - 1], bone.pos[idx], t);
			pos[model_bone_index] = lerp(pos[model_bone_index], anim_pos, weight);

			ASSERT(bone.rot_count > 1);
			idx = 1;
			for (u32 c = bone.rot_count; idx < c; ++idx) {
				if (bone.rot_times[idx] > anim_t) break;
			}

			t = float(anim_t - bone.rot_times[idx - 1]) / (bone.rot_times[idx] - bone.rot_times[idx - 1]);

			Quat anim_rot = nlerp(bone.rot[idx - 1], bone.rot[idx], t);
			rot[model_bone_index] = nlerp(rot[model_bone_index], anim_rot, weight);
		}
	}
	else {
		for (Bone& bone : m_bones) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(bone.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(bone.name) == mask->bones.end()) continue;

			int model_bone_index = iter.value();
			pos[model_bone_index] = lerp(pos[model_bone_index], bone.pos[bone.pos_count - 1], weight);
			rot[model_bone_index] = nlerp(rot[model_bone_index], bone.rot[bone.rot_count - 1], weight);
		}
	}
}


LocalRigidTransform Animation::getBoneTransform(Time time, int bone_idx) const
{
	LocalRigidTransform ret;

	const Bone& bone = m_bones[bone_idx];
	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		ASSERT(anim_t_highres <= 0xffFF);
		const u16 anim_t = u16(anim_t_highres);

		ASSERT(bone.pos_count > 1);
		u32 idx = 1;
		for (u32 c = bone.pos_count; idx < c; ++idx) {
			if (bone.pos_times[idx] > anim_t) break;
		}

		float t = float(anim_t - bone.pos_times[idx - 1]) / (bone.pos_times[idx] - bone.pos_times[idx - 1]);
		ret.pos = lerp(bone.pos[idx - 1], bone.pos[idx], t);

		ASSERT(bone.rot_count > 1);
		idx = 1;
		for (u32 c = bone.rot_count; idx < c; ++idx) {
			if (bone.rot_times[idx] > anim_t) break;
		}

		t = float(anim_t - bone.rot_times[idx - 1]) / (bone.rot_times[idx] - bone.rot_times[idx - 1]);
		ret.rot = nlerp(bone.rot[idx - 1], bone.rot[idx], t);
	}
	else {
		ret.pos = bone.pos[bone.pos_count - 1];
		ret.rot = bone.rot[bone.rot_count - 1];
	}
	return ret;
}


int Animation::getBoneIndex(u32 name) const
{
	for (int i = 0, c = m_bones.size(); i < c; ++i) {
		if (m_bones[i].name == name) return i;
	}
	return -1;
}


void Animation::getRelativePose(Time time, Pose& pose, const Model& model, const BoneMask* mask) const
{
	ASSERT(!pose.is_absolute);

	if (!model.isReady()) return;

	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;

	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		ASSERT(anim_t_highres <= 0xffFF);
		const u16 anim_t = u16(anim_t_highres);

		for (Bone& bone : m_bones) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(bone.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(bone.name) == mask->bones.end()) continue;

			int model_bone_index = iter.value();
			ASSERT(bone.pos_count > 1);
			u32 idx = 1;
			for (u32 c = bone.pos_count; idx < c; ++idx) {
				if (bone.pos_times[idx] > anim_t) break;
			}

			float t = float(anim_t - bone.pos_times[idx - 1]) / (bone.pos_times[idx] - bone.pos_times[idx - 1]);
			pos[model_bone_index] = lerp(bone.pos[idx - 1], bone.pos[idx], t);
			
			ASSERT(bone.rot_count > 1);
			idx = 1;
			for (u32 c = bone.rot_count; idx < c; ++idx) {
				if (bone.rot_times[idx] > anim_t) break;
			}

			t = float(anim_t - bone.rot_times[idx - 1]) / (bone.rot_times[idx] - bone.rot_times[idx - 1]);
			rot[model_bone_index] = nlerp(bone.rot[idx - 1], bone.rot[idx], t);
		}
	}
	else
	{
		for (Bone& bone : m_bones)
		{
			Model::BoneMap::const_iterator iter = model.getBoneIndex(bone.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(bone.name) == mask->bones.end()) continue;

			int model_bone_index = iter.value();
			pos[model_bone_index] = bone.pos[bone.pos_count - 1];
			rot[model_bone_index] = bone.rot[bone.rot_count - 1];
		}
	}
}


bool Animation::load(u64 mem_size, const u8* mem)
{
	m_bones.clear();
	m_mem.clear();
	Header header;
	InputMemoryStream file(mem, mem_size);
	file.read(&header, sizeof(header));
	if (header.magic != HEADER_MAGIC)
	{
		logError("Animation") << getPath() << " is not an animation file";
		return false;
	}
	if (header.version <= (int)Version::COMPRESSION)
	{
		logError("Animation") << "Unsupported animation version " << (int)header.version << " ("
									 << getPath() << ")";
		return false;
	}
	if (header.version > (int)Version::ROOT_MOTION)
	{
		file.read(&m_root_motion_bone_idx, sizeof(m_root_motion_bone_idx));
	}
	else
	{
		m_root_motion_bone_idx = -1;
	}
	m_length = header.length;
	u32 bone_count;
	file.read(&bone_count, sizeof(bone_count));
	m_bones.resize(bone_count);
	if (bone_count == 0) return true;

	int size = int(file.size() - file.getPosition());
	m_mem.resize(size);
	file.read(&m_mem[0], size);
	InputMemoryStream blob(&m_mem[0], size);
	for (int i = 0; i < m_bones.size(); ++i)
	{
		m_bones[i].name = blob.read<u32>();

		m_bones[i].pos_count = blob.read<int>();
		m_bones[i].pos_times = (const u16*)blob.skip(m_bones[i].pos_count * sizeof(u16));
		m_bones[i].pos = (const Vec3*)blob.skip(m_bones[i].pos_count * sizeof(Vec3));
		
		m_bones[i].rot_count = blob.read<int>();
		m_bones[i].rot_times = (const u16*)blob.skip(m_bones[i].rot_count * sizeof(u16));
		m_bones[i].rot = (const Quat*)blob.skip(m_bones[i].rot_count * sizeof(Quat));
	}

	m_size = file.size();
	return true;
}


void Animation::unload()
{
	m_bones.clear();
	m_mem.clear();
	m_length = Time::fromSeconds(0);
}


} // namespace Lumix
