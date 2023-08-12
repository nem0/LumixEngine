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
	, m_root_motion(m_allocator)
{
}


struct AnimationSampler {

	static LUMIX_FORCE_INLINE LocalRigidTransform maskRootMotion(Animation::Flags flags, const LocalRigidTransform& transform) {
		LocalRigidTransform root_motion;
		root_motion.pos = Vec3::ZERO;
		root_motion.rot = Quat::IDENTITY;
	
		if (flags & Animation::Y_ROOT_TRANSLATION) root_motion.pos.y = transform.pos.y;
		if (flags & Animation::XZ_ROOT_TRANSLATION) {
			root_motion.pos.x = transform.pos.x;
			root_motion.pos.z = transform.pos.z;
		}
	
		if (flags & Animation::ROOT_ROTATION) {
			root_motion.rot.y = transform.rot.y;
			root_motion.rot.w = transform.rot.w;
			root_motion.rot = normalize(root_motion.rot);
		}

		return root_motion;
	}

	template <bool use_mask, bool use_weight>
	static void getRelativePose(Animation& anim, const Animation::SampleContext& ctx) {
		Pose& pose = *ctx.pose;
		const Model& model = *ctx.model;
		const Time time = ctx.time;
		const BoneMask* mask = ctx.mask;
		const float weight = ctx.weight;

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

Animation::RootMotion::RootMotion(IAllocator& allocator)
	: translations(allocator)
	, rotations(allocator)
	, pose_translations(allocator)
	, pose_rotations(allocator)
{}

void Animation::setRootMotionBone(BoneNameHash bone_name) {
	if (m_root_motion.bone == bone_name) return;
	if ((m_flags & ANY_ROOT_MOTION) == 0) return;

	ASSERT(m_root_motion.bone.getHashValue() == 0);
	m_root_motion.bone = bone_name;

	i32 translation_idx = -1;
	for (i32 i = 0, c = m_translations.size(); i < c; ++i) {
		if (m_translations[i].name == bone_name) {
			translation_idx = i;
			break;
		}
	}

	i32 rotation_idx = -1;
	for (i32 i = 0, c = m_rotations.size(); i < c; ++i) {
		if (m_rotations[i].name == bone_name) {
			rotation_idx = i;
			break;
		}
	}
	
	m_root_motion.pose_translations.resize(m_frame_count);
	m_root_motion.pose_rotations.resize(m_frame_count);

	if (rotation_idx >= 0 && (m_flags & Animation::ROOT_ROTATION)) {
		m_root_motion.rotations.resize(m_frame_count);
	}

	if (translation_idx >= 0 && (m_flags & Animation::ANY_ROOT_TRANSLATION)) {
		m_root_motion.translations.resize(m_frame_count);
	}

	for (u32 i = 0; i < m_frame_count; ++i) {
		LocalRigidTransform tmp = {Vec3(0), Quat::IDENTITY};
		Time t = m_length * (i / float(m_frame_count - 1));
		if (translation_idx >= 0) tmp.pos = getTranslation(t, translation_idx);
		if (rotation_idx >= 0) tmp.rot = getRotation(t, rotation_idx);
		LocalRigidTransform rm = AnimationSampler::maskRootMotion(m_flags, tmp);
		if (!m_root_motion.translations.empty()) m_root_motion.translations[i] = rm.pos;
		if (!m_root_motion.rotations.empty()) m_root_motion.rotations[i] = rm.rot;

		LocalRigidTransform rm0;
		rm0.pos = m_root_motion.translations.empty() ? Vec3(0) : m_root_motion.translations[0];
		rm0.rot = m_root_motion.rotations.empty() ? Quat::IDENTITY : m_root_motion.rotations[0];

		tmp = rm0 * rm.inverted() * tmp;

		m_root_motion.pose_translations[i] = tmp.pos;
		m_root_motion.pose_rotations[i] = tmp.rot;
	}

	m_translations[translation_idx].count = m_frame_count;
	m_translations[translation_idx].times = nullptr;
	m_translations[translation_idx].pos = m_root_motion.pose_translations.begin();
	m_rotations[rotation_idx].count = m_frame_count;
	m_rotations[rotation_idx].times = nullptr;
	m_rotations[rotation_idx].rot = m_root_motion.pose_rotations.begin();
}

LocalRigidTransform Animation::getRootMotion(Time time) const {
	LocalRigidTransform tr = {Vec3(0), Quat::IDENTITY};
	
	if (time < m_length) {
		const u64 anim_t_highres = ((u64)time.raw() << 16) / (m_length.raw());
		const u64 frame_48_16 = (m_frame_count - 1) * anim_t_highres;
		ASSERT((frame_48_16 & 0xffFF00000000) == 0);
		const u32 frame_idx = u32(frame_48_16 >> 16);
		const float frame_t = (frame_48_16 & 0xffFF) / float(0xffFF);

		if (!m_root_motion.rotations.empty()) {
			tr.rot = nlerp(m_root_motion.rotations[frame_idx], m_root_motion.rotations[frame_idx + 1], frame_t);
		}
		if (!m_root_motion.translations.empty()) {
			tr.pos = lerp(m_root_motion.translations[frame_idx], m_root_motion.translations[frame_idx + 1], frame_t);
		}
		return tr;
	}
	
	if (!m_root_motion.rotations.empty()) tr.rot = m_root_motion.rotations[m_frame_count - 1];
	if (!m_root_motion.translations.empty()) tr.pos = m_root_motion.translations[m_frame_count - 1];
	return tr;
}


void Animation::getRelativePose(const SampleContext& ctx) {
	if (ctx.mask) {
		if (ctx.weight < 0.9999f) {
			AnimationSampler::getRelativePose<true, true>(*this, ctx);
		}
		else {
			AnimationSampler::getRelativePose<true, false>(*this, ctx);
		}
	}
	else {
		if (ctx.weight < 0.9999f) {
			AnimationSampler::getRelativePose<false, true>(*this, ctx);
		}
		else {
			AnimationSampler::getRelativePose<false, false>(*this, ctx);
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

bool Animation::load(Span<const u8> mem) {
	m_translations.clear();
	m_rotations.clear();
	m_mem.clear();
	Header header;
	InputMemoryStream file(mem);
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

	if (header.version > Version::FLAGS) {
		file.read(m_flags);
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
