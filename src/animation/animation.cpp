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

		float frame = time.toFrame(anim.m_fps);
		const u32 frame_idx = u32(frame);
		if (frame_idx < anim.m_frame_count - 1) {
			const float frame_t = frame - frame_idx;
		
			for (const Animation::TranslationCurve& curve : anim.m_translations) {
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;

				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				Vec3 anim_pos = lerp(curve.pos[frame_idx], curve.pos[frame_idx + 1], frame_t);

				const int model_bone_index = iter.value();
				if constexpr (use_weight) {
					pos[model_bone_index] = lerp(pos[model_bone_index], anim_pos, weight);
				}
				else {
					pos[model_bone_index] = anim_pos;
				}
			}

			for (u32 i = 0, c = anim.m_rotations.size(); i < c; ++i) {
				const Animation::RotationTrack& curve = anim.m_rotations[i];
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;
				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				Quat anim_rot = nlerp(anim.getRotation(frame_idx, i), anim.getRotation(frame_idx + 1, i), frame_t);

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
					pos[model_bone_index] = lerp(pos[model_bone_index], curve.pos[anim.m_frame_count - 1], weight);
				}
				else {
					pos[model_bone_index] = curve.pos[anim.m_frame_count - 1];
				}
			}

			for (u32 i = 0, c = anim.m_rotations.size(); i < c; ++i) {
				const Animation::RotationTrack& curve = anim.m_rotations[i];
				Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
				if (!iter.isValid()) continue;
				if constexpr(use_mask) {
					if (mask->bones.find(curve.name) == mask->bones.end()) continue;
				}

				const int model_bone_index = iter.value();
				if constexpr (use_weight) {
					rot[model_bone_index] = nlerp(rot[model_bone_index], anim.getRotation(anim.m_frame_count - 1, i), weight);
				}
				else {
					rot[model_bone_index] = anim.getRotation(anim.m_frame_count - 1, i);
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

	for (u32 f = 0; f < m_frame_count; ++f) {
		LocalRigidTransform tmp = {Vec3(0), Quat::IDENTITY};
		if (translation_idx >= 0) tmp.pos = m_translations[translation_idx].pos[f];
		if (rotation_idx >= 0) tmp.rot = getRotation(f, rotation_idx);
		LocalRigidTransform rm = AnimationSampler::maskRootMotion(m_flags, tmp);
		if (!m_root_motion.translations.empty()) m_root_motion.translations[f] = rm.pos;
		if (!m_root_motion.rotations.empty()) m_root_motion.rotations[f] = rm.rot;

		LocalRigidTransform rm0;
		rm0.pos = m_root_motion.translations.empty() ? Vec3(0) : m_root_motion.translations[0];
		rm0.rot = m_root_motion.rotations.empty() ? Quat::IDENTITY : m_root_motion.rotations[0];

		tmp = rm0 * rm.inverted() * tmp;

		m_root_motion.pose_translations[f] = tmp.pos;
		m_root_motion.pose_rotations[f] = tmp.rot;
	}

	m_translations[translation_idx].pos = m_root_motion.pose_translations.begin();
	// TODO
	//m_rotations[rotation_idx].rot = m_root_motion.pose_rotations.begin();
}

LocalRigidTransform Animation::getRootMotion(Time time) const {
	LocalRigidTransform tr = {Vec3(0), Quat::IDENTITY};
	float frame = time.toFrame(m_fps);
	const u32 frame_idx = u32(frame);
	if (frame_idx < m_frame_count - 1) {
		const float frame_t = frame - frame_idx;

		if (!m_root_motion.rotations.empty()) {
			tr.rot = nlerp(m_root_motion.rotations[frame_idx], m_root_motion.rotations[frame_idx + 1], frame_t);
		}
		if (!m_root_motion.translations.empty()) {
			tr.pos = lerp(m_root_motion.translations[frame_idx], m_root_motion.translations[frame_idx + 1], frame_t);
		}
		return tr;
	}
	
	if (!m_root_motion.rotations.empty()) tr.rot = m_root_motion.rotations.back();
	if (!m_root_motion.translations.empty()) tr.pos = m_root_motion.translations.back();
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

Vec3 Animation::getTranslation(Time time, u32 curve_idx) const {
	const TranslationCurve& curve = m_translations[curve_idx];
	float frame = time.toFrame(m_fps);
	const u32 frame_idx = u32(frame);
	
	if (frame_idx < m_frame_count - 1) {
		const float frame_t = frame - frame_idx;
		return lerp(curve.pos[frame_idx], curve.pos[frame_idx + 1], frame_t);
	}

	return curve.pos[m_frame_count - 1];
}

float Animation::unpackRotationChannel(u64 val, float min, float range, u32 bitsize) const {
	if (bitsize == 0) return min;
	return float(min + range * double(val & ((1 << bitsize) - 1)) / ((1 << bitsize) - 1));
}

Quat Animation::getRotation(u32 frame, u32 curve_idx) const {
	const RotationTrack& track = m_rotations[curve_idx];
	if (track.type == Animation::TrackType::CONSTANT) return track.min;
	const u32 offset = m_rotations_frame_size_bits * frame + track.offset_bits;

	u64 tmp;
	i32 rem = track.bitsizes_sum - (64 - (offset % 64));
	if (rem <= 0) {
		u64 buf;
		memcpy(&buf, m_rotation_stream + offset / 64 * 8, sizeof(buf));
		u32 offset0 = (64 - (offset % 64) - track.bitsizes_sum);
		tmp = buf >> offset0;
	}
	else {
		u64 buf[2];
		memcpy(&buf, m_rotation_stream + offset / 64 * 8, sizeof(buf));
		tmp = buf[0];
		tmp <<= rem;
		tmp |= buf[1] >> (64 - rem);
	}

	Quat res;
	res.x = unpackRotationChannel(tmp, track.min.x, track.range.x, track.bitsizes[0]);
	tmp >>= track.bitsizes[0];
	res.y = unpackRotationChannel(tmp, track.min.y, track.range.y, track.bitsizes[1]);
	tmp >>= track.bitsizes[1];
	res.z = unpackRotationChannel(tmp, track.min.z, track.range.z, track.bitsizes[2]);
	tmp >>= track.bitsizes[2];
	res.w = unpackRotationChannel(tmp, track.min.w, track.range.w, track.bitsizes[3]);
	return res;
}

Quat Animation::getRotation(Time time, u32 curve_idx) const {
	float frame = time.toFrame(m_fps);
	const u32 frame_idx = u32(frame);
	
	if (frame_idx < m_frame_count - 1) {
		const float frame_t = frame - frame_idx;
		return nlerp(getRotation(frame_idx, curve_idx), getRotation(frame_idx + 1, curve_idx), frame_t);
	}

	return getRotation(m_frame_count - 1, curve_idx);
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

	if (header.version <= Version::COMPRESSION) {
		logError(getPath(), ": version not supported. Please delete '.lumix' directory and try again");
		return false;
	}

	file.read(m_fps);
	file.read(m_frame_count);
	file.read(m_flags);

	u32 translations_count;
	file.read(&translations_count, sizeof(translations_count));
	const u32 size = u32(file.size() - file.getPosition());
	m_mem.resize(size + 8/*padding for unpacker*/);
	file.read(&m_mem[0], size);

	m_translations.resize(translations_count);

	InputMemoryStream blob(&m_mem[0], size);
	for (int i = 0; i < m_translations.size(); ++i) {
		TranslationCurve& curve = m_translations[i];
		curve.name = blob.read<BoneNameHash>();
		const Animation::TrackType type = blob.read<Animation::TrackType>();
		curve.pos = (const Vec3*)blob.skip(m_frame_count * sizeof(Vec3));
	}
	
	const u32 rotations_count = blob.read<u32>();
	m_rotations.resize(rotations_count);

	m_rotations_frame_size_bits = 0;
	for (int i = 0; i < m_rotations.size(); ++i) {
		RotationTrack& curve = m_rotations[i];
		curve.name = blob.read<BoneNameHash>();
		blob.read(curve.type);
		
		if (curve.type == Animation::TrackType::CONSTANT) {
			blob.read(curve.min);
		}
		else {
			blob.read(curve.min);
			blob.read(curve.range);
			blob.read(curve.bitsizes);
			blob.read(curve.bitsizes_sum);
			blob.read(curve.offset_bits);
			m_rotations_frame_size_bits += curve.bitsizes_sum;
		}
	}

	m_rotation_stream = (const u8*)blob.skip(0);

	return true;
}


void Animation::unload()
{
	m_translations.clear();
	m_rotations.clear();
	m_mem.clear();
	m_frame_count = 0;
}


} // namespace Lumix
