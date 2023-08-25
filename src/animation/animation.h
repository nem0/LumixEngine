#pragma once

#include "engine/allocators.h"
#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/math.h"
#include "engine/resource.h"
#include "engine/string.h"

namespace Lumix
{

struct Model;
struct Pose;
struct Quat;
struct Vec3;

struct Time {
	Time() {}
	explicit Time(u32 v) : value(v) {}
	static Time fromSeconds(float time) {
		ASSERT(time >= 0);
		return Time{u32(time * ONE_SECOND)};
	}
	float seconds() const { return float(value / double(ONE_SECOND)); }
	float toFrame(float fps) const { return float(value / double(ONE_SECOND) * fps); }
	Time operator*(float t) const { return Time{u32(value * t)}; }
	float operator/(const Time& rhs) const { return float(double(value) / double(rhs.value)); }
	Time operator+(const Time& rhs) const { return Time{value + rhs.value}; }
	Time operator-(const Time& rhs) const { ASSERT(value >= rhs.value); return Time{value - rhs.value}; }
	void operator+=(const Time& rhs) { value += rhs.value; }
	bool operator<(const Time& rhs) const { return value < rhs.value; }
	bool operator>(const Time& rhs) const { return value > rhs.value; }
	bool operator<=(const Time& rhs) const { return value <= rhs.value; }
	bool operator>=(const Time& rhs) const { return value >= rhs.value; }
	Time operator%(const Time& rhs) const { return Time{value % rhs.value}; }
	u32 raw() const { return value; }

private:
	u32 value;
	enum { ONE_SECOND = 1 << 15 };
};


struct BoneMask
{
	BoneMask(IAllocator& allocator) : bones(allocator), name(allocator) {}
	BoneMask(BoneMask&& rhs) = default;
	String name;
	HashMap<BoneNameHash, u8> bones;
};


struct Animation final : Resource {
	friend struct AnimationSampler;
	static const u32 HEADER_MAGIC = 0x5f4c4146; // '_LAF'
	static const ResourceType TYPE;

	enum class TrackType : u8 {
		CONSTANT,
		ANIMATED
	};

	enum class Version : u32 {
		COMPRESSION = 6,

		LAST
	};

	enum Flags : u32 {
		NONE = 0,
		Y_ROOT_TRANSLATION = 1 << 0,
		XZ_ROOT_TRANSLATION = 1 << 1,
		ROOT_ROTATION = 1 << 2,

		ANY_ROOT_MOTION = Y_ROOT_TRANSLATION | XZ_ROOT_TRANSLATION | ROOT_ROTATION,
		ANY_ROOT_TRANSLATION = Y_ROOT_TRANSLATION | XZ_ROOT_TRANSLATION
	};

	struct Header {
		u32 magic;
		Version version;
	};

	struct ConstTranslationTrack {
		BoneNameHash name;
		Vec3 value;
	};

	struct TranslationTrack {
		BoneNameHash name;
		Vec3 min;
		Vec3 to_range; //  * to_normalized * (max - min)
		u16 offset_bits = 0;
		u8 bitsizes[3] = {};
	};

	struct ConstRotationTrack {
		BoneNameHash name;
		Quat value;
	};

	struct RotationTrack {
		BoneNameHash name;
		Vec3 min;
		Vec3 to_range; //  * to_normalized * (max - min)
		u16 offset_bits;
		u8 bitsizes[3];
		u8 skipped_channel;
	};

	struct SampleContext {
		Pose* pose;
		const Model* model;
		Time time;
		float weight = 1;
		const BoneMask* mask = nullptr;
	};

	Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	ResourceType getType() const override { return TYPE; }
	void getRelativePose(const SampleContext& ctx);
	Time getLength() const { return Time::fromSeconds(m_frame_count / m_fps); }

	Vec3 getTranslation(u32 frame, const TranslationTrack& track) const;
	Quat getRotation(u32 sample, const RotationTrack& track) const;
	
	const Array<TranslationTrack>& getTranslations() const { return m_translations; }
	const Array<ConstTranslationTrack>& getConstTranslations() const { return m_const_translations; }
	const Array<RotationTrack>& getRotations() const { return m_rotations; }
	const Array<ConstRotationTrack>& getConstRotations() const { return m_const_rotations; }
	struct LocalRigidTransform getRootMotion(Time t) const;
	void setRootMotionBone(BoneNameHash bone_name);
	u32 getFramesCount() const { return m_frame_count; }
	u32 getRotationFrameSizeBits() const { return m_rotations_frame_size_bits; }
	u32 getTranslationFrameSizeBits() const { return m_translations_frame_size_bits; }
	Flags m_flags = Flags::NONE;

private:
	void unload() override;
	bool load(Span<const u8> mem) override;

	TagAllocator m_allocator;
	Array<TranslationTrack> m_translations;
	Array<ConstTranslationTrack> m_const_translations;
	Array<RotationTrack> m_rotations;
	Array<ConstRotationTrack> m_const_rotations;
	
	struct RootMotion {
		RootMotion(IAllocator&);
		Array<Vec3> pose_translations;
		Array<Quat> pose_rotations;
		Array<Vec3> translations;
		Array<Quat> rotations;
		BoneNameHash bone;
		i32 rotation_track_idx = -1;
		i32 translation_track_idx = -1;
	} m_root_motion;

	Array<u8> m_mem;
	const u8* m_rotation_stream;
	const u8* m_translation_stream;
	u32 m_rotations_frame_size_bits;
	u32 m_translations_frame_size_bits;
	u32 m_frame_count = 0;
	float m_fps = 30;

	friend struct AnimationSampler;
};


inline Time lerp(Time op1, Time op2, float t) {
	const double d = double(op1.raw()) * (1 - t) + double(op2.raw()) * t;
	return Time(u32(d));
}

} // namespace Lumix
