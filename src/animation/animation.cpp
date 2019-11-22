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

	LAST
};


const ResourceType Animation::TYPE("animation");


Animation::Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_mem(allocator)
	, m_translations(allocator)
	, m_rotations(allocator)
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
		
		for (const TranslationCurve& curve : m_translations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			u32 idx = 1;
			for (u32 c = curve.count; idx < c; ++idx) {
				if (curve.times[idx] > anim_t) break;
			}

			const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);

			const int model_bone_index = iter.value();
			const Vec3 anim_pos = lerp(curve.pos[idx - 1], curve.pos[idx], t);
			pos[model_bone_index] = lerp(pos[model_bone_index], anim_pos, weight);
		}

		for (const RotationCurve& curve : m_rotations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			u32 idx = 1;
			for (u32 c = curve.count; idx < c; ++idx) {
				if (curve.times[idx] > anim_t) break;
			}

			const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);

			const int model_bone_index = iter.value();
			Quat anim_rot = nlerp(curve.rot[idx - 1], curve.rot[idx], t);
			rot[model_bone_index] = nlerp(rot[model_bone_index], anim_rot, weight);
		}
	}
	else {
		for (const TranslationCurve& curve : m_translations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			const int model_bone_index = iter.value();
			pos[model_bone_index] = lerp(pos[model_bone_index], curve.pos[curve.count - 1], weight);
		}

		for (const RotationCurve& curve : m_rotations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			const int model_bone_index = iter.value();
			rot[model_bone_index] = nlerp(rot[model_bone_index], curve.rot[curve.count - 1], weight);
		}
	}
}


Vec3 Animation::getTranslation(Time time, u32 curve_idx) const
{
	const TranslationCurve& curve = m_translations[curve_idx];
	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		ASSERT(anim_t_highres <= 0xffFF);
		const u16 anim_t = u16(anim_t_highres);

		u32 idx = 1;
		for (u32 c = curve.count; idx < c; ++idx) {
			if (curve.times[idx] > anim_t) break;
		}

		const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);
		return lerp(curve.pos[idx - 1], curve.pos[idx], t);
	}

	return curve.pos[curve.count - 1];
}

int Animation::getTranslationCurveIndex(u32 name_hash) const {
	for (int i = 0, c = m_translations.size(); i < c; ++i) {
		if (m_translations[i].name == name_hash) return i;
	}
	return -1;
}

int Animation::getRotationCurveIndex(u32 name_hash) const {
	for (int i = 0, c = m_rotations.size(); i < c; ++i) {
		if (m_rotations[i].name == name_hash) return i;
	}
	return -1;
}

Quat Animation::getRotation(Time time, u32 curve_idx) const
{
	const RotationCurve& curve = m_rotations[curve_idx];
	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		ASSERT(anim_t_highres <= 0xffFF);
		const u16 anim_t = u16(anim_t_highres);

		u32 idx = 1;
		for (u32 c = curve.count; idx < c; ++idx) {
			if (curve.times[idx] > anim_t) break;
		}

		const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);
		return nlerp(curve.rot[idx - 1], curve.rot[idx], t);
	}

	return curve.rot[curve.count - 1];
}


void Animation::getRelativePose(Time time, Pose& pose, const Model& model, const BoneMask* mask) const
{
#if 0
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
#endif
	ASSERT(!pose.is_absolute);

	if (!model.isReady()) return;

	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;

	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		ASSERT(anim_t_highres <= 0xffFF);
		const u16 anim_t = u16(anim_t_highres);
		
		for (const TranslationCurve& curve : m_translations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			ASSERT(curve.count > 1);
			u32 idx = 1;
			for (u32 c = curve.count; idx < c; ++idx) {
				if (curve.times[idx] > anim_t) break;
			}

			const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);

			const int model_bone_index = iter.value();
			pos[model_bone_index] = lerp(curve.pos[idx - 1], curve.pos[idx], t);
		}

		for (const RotationCurve& curve : m_rotations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			ASSERT(curve.count > 1);
			u32 idx = 1;
			for (u32 c = curve.count; idx < c; ++idx) {
				if (curve.times[idx] > anim_t) break;
			}

			const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);

			const int model_bone_index = iter.value();
			rot[model_bone_index] = nlerp(curve.rot[idx - 1], curve.rot[idx], t);
		}
	}
	else {
		for (const TranslationCurve& curve : m_translations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			const int model_bone_index = iter.value();
			pos[model_bone_index] = curve.pos[curve.count - 1];
		}

		for (const RotationCurve& curve : m_rotations) {
			Model::BoneMap::const_iterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if (mask && mask->bones.find(curve.name) == mask->bones.end()) continue;

			const int model_bone_index = iter.value();
			rot[model_bone_index] = curve.rot[curve.count - 1];
		}
	}
}


bool Animation::load(u64 mem_size, const u8* mem)
{
	m_translations.clear();
	m_rotations.clear();
	m_mem.clear();
	Header header;
	InputMemoryStream file(mem, mem_size);
	file.read(&header, sizeof(header));
	if (header.magic != HEADER_MAGIC) {
		logError("Animation") << getPath() << " is not an animation file";
		return false;
	}

	file.read(&m_root_motion_bone_idx, sizeof(m_root_motion_bone_idx));
	m_length = header.length;
	u32 translations_count;
	file.read(&translations_count, sizeof(translations_count));
	const u32 size = u32(file.size() - file.getPosition());
	m_mem.resize(size);
	file.read(&m_mem[0], size);

	m_translations.resize(translations_count);

	InputMemoryStream blob(&m_mem[0], size);
	for (int i = 0; i < m_translations.size(); ++i) {
		TranslationCurve& curve = m_translations[i];
		curve.name = blob.read<u32>();
		curve.count = blob.read<u32>();
		ASSERT(curve.count > 1);
		curve.times = (const u16*)blob.skip(curve.count * sizeof(u16));
		curve.pos = (const Vec3*)blob.skip(curve.count * sizeof(Vec3));
	}
	
	const u32 rotations_count = blob.read<u32>();
	m_rotations.resize(rotations_count);

	for (int i = 0; i < m_rotations.size(); ++i) {
		RotationCurve& curve = m_rotations[i];
		curve.name = blob.read<u32>();
		curve.count = blob.read<u32>();
		ASSERT(curve.count > 1);
		curve.times = (const u16*)blob.skip(curve.count * sizeof(u16));
		curve.rot = (const Quat*)blob.skip(curve.count * sizeof(Quat));
	}

	m_size = file.size();
	return true;
}


void Animation::unload()
{
	m_translations.clear();
	m_rotations.clear();
	m_mem.clear();
	m_length = Time::fromSeconds(0);
}


} // namespace Lumix
