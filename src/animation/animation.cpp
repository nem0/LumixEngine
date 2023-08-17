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
	, m_const_rotations(m_allocator)
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

		float sample = clamp(time.toFrame(anim.m_fps), 0.f, anim.m_frame_count - 0.00001f);
		const u32 sample_idx = u32(sample);
		const float t = sample - sample_idx;
		
		for (u32 i = 0, c = anim.m_translations.size(); i < c; ++i) {
			const Animation::TranslationTrack& curve = anim.m_translations[i];
			Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;

			if constexpr(use_mask) {
				if (mask->bones.find(curve.name) == mask->bones.end()) continue;
			}

			Vec3 anim_pos = lerp(anim.getTranslation(sample_idx, i), anim.getTranslation(sample_idx + 1, i), t);

			const int model_bone_index = iter.value();
			if constexpr (use_weight) {
				pos[model_bone_index] = lerp(pos[model_bone_index], anim_pos, weight);
			}
			else {
				pos[model_bone_index] = anim_pos;
			}
		}

		for (u32 i = 0, c = anim.m_const_rotations.size(); i < c; ++i) {
			const Animation::ConstRotationTrack& curve = anim.m_const_rotations[i];
			Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if constexpr(use_mask) {
				if (mask->bones.find(curve.name) == mask->bones.end()) continue;
			}

			const int model_bone_index = iter.value();
			if constexpr (use_weight) {
				rot[model_bone_index] = nlerp(rot[model_bone_index], curve.value, weight);
			}
			else {
				rot[model_bone_index] = curve.value;
			}
		}

		for (u32 i = 0, c = anim.m_rotations.size(); i < c; ++i) {
			const Animation::RotationTrack& curve = anim.m_rotations[i];
			Model::BoneMap::ConstIterator iter = model.getBoneIndex(curve.name);
			if (!iter.isValid()) continue;
			if constexpr(use_mask) {
				if (mask->bones.find(curve.name) == mask->bones.end()) continue;
			}

			Quat anim_rot = nlerp(anim.getRotation(sample_idx, curve), anim.getRotation(sample_idx + 1, curve), t);

			const int model_bone_index = iter.value();
			if constexpr (use_weight) {
				rot[model_bone_index] = nlerp(rot[model_bone_index], anim_rot, weight);
			}
			else {
				rot[model_bone_index] = anim_rot;
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
	
	m_root_motion.pose_translations.resize(m_frame_count + 1);
	m_root_motion.pose_rotations.resize(m_frame_count + 1);

	if (rotation_idx >= 0 && (m_flags & Animation::ROOT_ROTATION)) {
		m_root_motion.rotations.resize(m_frame_count + 1);
	}

	if (translation_idx >= 0 && (m_flags & Animation::ANY_ROOT_TRANSLATION)) {
		m_root_motion.translations.resize(m_frame_count + 1);
	}

	for (u32 f = 0; f < m_frame_count + 1; ++f) {
		LocalRigidTransform tmp = {Vec3(0), Quat::IDENTITY};
		if (translation_idx >= 0) tmp.pos = getTranslation(f, translation_idx);
		if (rotation_idx >= 0) tmp.rot = getRotation(f, m_rotations[rotation_idx]);
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

	m_root_motion.rotation_track_idx = rotation_idx;
	m_translations[translation_idx].type = Animation::TrackType::ROOT_MOTION_ROOT;
}

LocalRigidTransform Animation::getRootMotion(Time time) const {
	LocalRigidTransform tr = {Vec3(0), Quat::IDENTITY};
	float frame = time.toFrame(m_fps);
	const u32 frame_idx = u32(frame);
	if (frame_idx < m_frame_count) {
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
	float frame = time.toFrame(m_fps);
	const u32 frame_idx = u32(frame);
	
	if (frame_idx < m_frame_count) {
		const float frame_t = frame - frame_idx;
		return lerp(getTranslation(frame_idx, curve_idx), getTranslation(frame_idx + 1, curve_idx), frame_t);
	}

	return getTranslation(m_frame_count, curve_idx);
}

static float unpackChannel(u64 val, float min, float to_float_range, u32 bitsize) {
	if (bitsize == 0) return min;
	const u64 mask = (u64(1) << bitsize) - 1;
	return float(min + to_float_range * double(val & mask));
}

Vec3 Animation::getTranslation(u32 frame, u32 curve_idx) const {
	const TranslationTrack& track = m_translations[curve_idx];
	switch (track.type) {
		case Animation::TrackType::CONSTANT: return track.min;
		case Animation::TrackType::ROOT_MOTION_ROOT: return m_root_motion.pose_translations[frame];
		case Animation::TrackType::SAMPLED: {
			const u32 offset = m_translations_frame_size_bits * frame + track.offset_bits;

			u64 tmp;
			memcpy(&tmp, &m_translation_stream[offset / 8], sizeof(tmp));
			tmp >>= offset & 7;

			Vec3 res;
			res.x = unpackChannel(tmp, track.min.x, track.to_range.x, track.bitsizes[0]);
			tmp >>= track.bitsizes[0];
			res.y = unpackChannel(tmp, track.min.y, track.to_range.y, track.bitsizes[1]);
			tmp >>= track.bitsizes[1];
			res.z = unpackChannel(tmp, track.min.z, track.to_range.z, track.bitsizes[2]);
			return res;
		}
	}
	ASSERT(false);
	return {};
}

Quat Animation::getRotation(u32 frame, const RotationTrack& track) const {
	ASSERT(&track >= m_rotations.begin() && &track < m_rotations.end());
	if (&track - m_rotations.begin() == m_root_motion.rotation_track_idx) return m_root_motion.pose_rotations[frame];
	const u32 offset = m_rotations_frame_size_bits * frame + track.offset_bits;

	u64 packed;
	memcpy(&packed, &m_rotation_stream[offset / 8], sizeof(packed));
	packed >>= offset & 7;

	bool is_negative = packed & 1;
	packed >>= 1;
	Vec3 v3;

	v3.x = unpackChannel(packed, track.min.x, track.to_range.x, track.bitsizes[0]);
	packed >>= track.bitsizes[0];
	v3.y = unpackChannel(packed, track.min.y, track.to_range.y, track.bitsizes[1]);
	packed >>= track.bitsizes[1];
	v3.z = unpackChannel(packed, track.min.z, track.to_range.z, track.bitsizes[2]);
	float skipped = sqrtf(maximum(0.f, 1 - dot(v3, v3))) * (is_negative ? -1 : 1);

	switch (track.skipped_channel) {
		case 0: return Quat(skipped, v3.x, v3.y, v3.z);
		case 1: return Quat(v3.x, skipped, v3.y, v3.z);
		case 2: return Quat(v3.x, v3.y, skipped, v3.z);
		case 3: return Quat(v3.x, v3.y, v3.z, skipped);
	}
	ASSERT(false);
	return {};
}

bool Animation::load(Span<const u8> mem) {
	m_translations.clear();
	m_rotations.clear();
	m_const_rotations.clear();
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

	m_translations_frame_size_bits = 0;
	InputMemoryStream blob(&m_mem[0], size);
	for (int i = 0; i < m_translations.size(); ++i) {
		TranslationTrack& track = m_translations[i];
		track.name = blob.read<BoneNameHash>();
		blob.read(track.type);
		
		if (track.type == Animation::TrackType::CONSTANT) {
			blob.read(track.min);
		}
		else {
			blob.read(track.min);
			Vec3 range;
			blob.read(track.to_range);
			blob.read(track.bitsizes);
			blob.read(track.offset_bits);
			m_translations_frame_size_bits += track.bitsizes[0] + track.bitsizes[1] + track.bitsizes[2];
		}
	}
	
	m_translation_stream = (const u8*)blob.skip((m_translations_frame_size_bits * (m_frame_count + 1) + 7) / 8);

	const u32 rotations_count = blob.read<u32>();

	m_rotations_frame_size_bits = 0;
	for (u32 i = 0; i < rotations_count; ++i) {
		auto bone_name_hash = blob.read<BoneNameHash>();
		auto type = blob.read<Animation::TrackType>();

		if (type == Animation::TrackType::CONSTANT) {
			ConstRotationTrack& track = m_const_rotations.emplace();
			track.name = bone_name_hash;
			blob.read(track.value);
		}
		else {
			RotationTrack& track = m_rotations.emplace();
			track.name = bone_name_hash;
			blob.read(track.min);
			blob.read(track.to_range);
			blob.read(track.bitsizes);
			blob.read(track.offset_bits);
			blob.read(track.skipped_channel);
			m_rotations_frame_size_bits += track.bitsizes[0] + track.bitsizes[1] + track.bitsizes[2] + 1/*sign bit*/;
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
