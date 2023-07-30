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


const ResourceType Animation::TYPE("animation");


Animation::Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator, m_path.c_str())
	, m_mem(m_allocator)
	, m_translations(m_allocator)
	, m_rotations(m_allocator)
{
}


struct AnimationSampler {
	template <bool use_mask, bool use_weight>
	static void getRelativePose(const Animation& anim, Time time, Pose& pose, const Model& model, float weight, const BoneMask* mask) {
		ASSERT(!pose.is_absolute);
		ASSERT(model.isReady());

		Vec3* pos = pose.positions;
		Quat* rot = pose.rotations;

		if (time < anim.getLength()) {
			const u64 anim_t_highres = ((u64)time.raw() << 16) / (anim.m_length.raw());
			ASSERT(anim_t_highres <= 0xffFF);
			const u16 anim_t = u16(anim_t_highres);
			const u64 frame_48_16 = (anim.m_frame_count - 1) * anim_t_highres;
			ASSERT((frame_48_16 & 0xffFF00000000) == 0);
			const u32 frame_idx = u32(frame_48_16 >> 16);
			const float frame_t = (frame_48_16 & 0xffFF) / float(0xffFF);
		
			for (const Animation::TranslationCurve& curve : anim.m_translations) {
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;
				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				Vec3 anim_pos;
				if (curve.times) {
					u32 idx = 1;
					for (u32 c = curve.count; idx < c; ++idx) {
						if (curve.times[idx] > anim_t) break;
					}
					const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);
					anim_pos = lerp(curve.pos[idx - 1], curve.pos[idx], t);
				}
				else {
					anim_pos = lerp(curve.pos[frame_idx], curve.pos[frame_idx + 1], frame_t);
				}

				const int model_bone_index = iter.value();
				if constexpr (use_weight) {
					pos[model_bone_index] = lerp(pos[model_bone_index], anim_pos, weight);
				}
				else {
					pos[model_bone_index] = anim_pos;
				}
			}

			for (const Animation::RotationCurve& curve : anim.m_rotations) {
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;
				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				Quat anim_rot;
				if(curve.times) {
					u32 idx = 1;
					for (u32 c = curve.count; idx < c; ++idx) {
						if (curve.times[idx] > anim_t) break;
					}
					const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);
					anim_rot = nlerp(curve.rot[idx - 1], curve.rot[idx], t);
				}
				else {
					anim_rot = nlerp(curve.rot[frame_idx], curve.rot[frame_idx + 1], frame_t);
				}

				const int model_bone_index = iter.value();
				if constexpr (use_weight) {
					rot[model_bone_index] = nlerp(rot[model_bone_index], anim_rot, weight);
				}
				else {
					rot[model_bone_index] = anim_rot;
				}
			}
		}
		else {
			for (const Animation::TranslationCurve& curve : anim.m_translations) {
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;
				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				const int model_bone_index = iter.value();
				if constexpr (use_weight) {
					pos[model_bone_index] = lerp(pos[model_bone_index], curve.pos[curve.count - 1], weight);
				}
				else {
					pos[model_bone_index] = curve.pos[curve.count - 1];
				}
			}

			for (const Animation::RotationCurve& curve : anim.m_rotations) {
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;
				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				const int model_bone_index = iter.value();
				if constexpr (use_weight) {
					rot[model_bone_index] = nlerp(rot[model_bone_index], curve.rot[curve.count - 1], weight);
				}
				else {
					rot[model_bone_index] = curve.rot[curve.count - 1];
				}
			}
		}
	}
}; // AnimationSampler

void Animation::getRelativePose(Time time, Pose& pose, const Model& model, float weight, const BoneMask* mask) const {
	if (mask) {
		if (weight < 0.9999f) {
			AnimationSampler::getRelativePose<true, true>(*this, time, pose, model, weight, mask);
		}
		else {
			AnimationSampler::getRelativePose<true, false>(*this, time, pose, model, weight, mask);
		}
	}
	else {
		if (weight < 0.9999f) {
			AnimationSampler::getRelativePose<false, true>(*this, time, pose, model, weight, mask);
		}
		else {
			AnimationSampler::getRelativePose<false, false>(*this, time, pose, model, weight, mask);
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

		if (curve.times) {
			u32 idx = 1;
			for (u32 c = curve.count; idx < c; ++idx) {
				if (curve.times[idx] > anim_t) break;
			}

			const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);
			return lerp(curve.pos[idx - 1], curve.pos[idx], t);
		}

		const u64 frame_48_16 = (m_frame_count - 1) * anim_t_highres;
		ASSERT((frame_48_16 & 0xffFF00000000) == 0);
		const u32 frame_idx = u32(frame_48_16 >> 16);
		const float frame_t = (frame_48_16 & 0xffFF) / float(0xffFF);

		return lerp(curve.pos[frame_idx], curve.pos[frame_idx + 1], frame_t);
	}

	return curve.pos[curve.count - 1];
}

int Animation::getTranslationCurveIndex(BoneNameHash name_hash) const {
	for (int i = 0, c = m_translations.size(); i < c; ++i) {
		if (m_translations[i].name == name_hash) return i;
	}
	return -1;
}

int Animation::getRotationCurveIndex(BoneNameHash name_hash) const {
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

		if (curve.times) {
			u32 idx = 1;
			for (u32 c = curve.count; idx < c; ++idx) {
				if (curve.times[idx] > anim_t) break;
			}

			const float t = float(anim_t - curve.times[idx - 1]) / (curve.times[idx] - curve.times[idx - 1]);
			return nlerp(curve.rot[idx - 1], curve.rot[idx], t);
		}

		const u64 frame_48_16 = (m_frame_count - 1) * anim_t_highres;
		ASSERT((frame_48_16 & 0xffFF00000000) == 0);
		const u32 frame_idx = u32(frame_48_16 >> 16);
		const float frame_t = (frame_48_16 & 0xffFF) / float(0xffFF);

		return nlerp(curve.rot[frame_idx], curve.rot[frame_idx + 1], frame_t);
	}

	return curve.rot[curve.count - 1];
}

void Animation::getRelativePose(Time time, Pose& pose, const Model& model, const BoneMask* mask) const {
	if(mask) {
		AnimationSampler::getRelativePose<true, false>(*this, time, pose, model, 1, mask);
	}
	else {
		AnimationSampler::getRelativePose<false, false>(*this, time, pose, model, 1, mask);
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
		logError("Invalid animation file ", getPath());
		return false;
	}

	if (header.version > Version::LAST) {
		logError(getPath(), ": version not supported");
		return false;
	}

	if (header.version <= Version::FIRST) {
		logError(getPath(), ": version not supported. Please delete '.lumix' directory and try again");
		return false;
	}

	m_length = header.length;
	m_frame_count = header.frame_count;
	u32 translations_count;
	file.read(&translations_count, sizeof(translations_count));
	const u32 size = u32(file.size() - file.getPosition());
	m_mem.resize(size);
	file.read(&m_mem[0], size);

	m_translations.resize(translations_count);

	InputMemoryStream blob(&m_mem[0], size);
	for (int i = 0; i < m_translations.size(); ++i) {
		TranslationCurve& curve = m_translations[i];
		curve.name = blob.read<BoneNameHash>();
		const Animation::CurveType type = blob.read<Animation::CurveType>();
		curve.count = blob.read<u32>();
		ASSERT(curve.count > 1 || type != Animation::CurveType::KEYFRAMED);
		curve.times = type == Animation::CurveType::KEYFRAMED ? (const u16*)blob.skip(curve.count * sizeof(u16)) : nullptr;
		curve.pos = (const Vec3*)blob.skip(curve.count * sizeof(Vec3));
	}
	
	const u32 rotations_count = blob.read<u32>();
	m_rotations.resize(rotations_count);

	for (int i = 0; i < m_rotations.size(); ++i) {
		RotationCurve& curve = m_rotations[i];
		curve.name = blob.read<BoneNameHash>();
		const Animation::CurveType type = blob.read<Animation::CurveType>();
		curve.count = blob.read<u32>();
		ASSERT(curve.count > 1 || type != Animation::CurveType::KEYFRAMED);
		curve.times = type == Animation::CurveType::KEYFRAMED ? (const u16*)blob.skip(curve.count * sizeof(u16)) : nullptr;
		curve.rot = (const Quat*)blob.skip(curve.count * sizeof(Quat));
	}

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
