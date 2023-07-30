#pragma once

#include "engine/allocators.h"
#include "engine/hash.h"
#include "engine/hash_map.h"
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
	Time operator*(float t) const { return Time{u32(value * t)}; }
	float operator/(const Time& rhs) const { return float(double(value) / double(rhs.value)); }
	Time operator+(const Time& rhs) const { return Time{value + rhs.value}; }
	Time operator-(const Time& rhs) const { return Time{value - rhs.value}; }
	void operator+=(const Time& rhs) { value += rhs.value; }
	bool operator<(const Time& rhs) const { return value < rhs.value; }
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
	BoneMask(IAllocator& allocator) : bones(allocator) {}
	BoneMask(BoneMask&& rhs) = default;
	StaticString<32> name;
	HashMap<BoneNameHash, u8> bones;
};


struct Animation final : Resource {
	static const u32 HEADER_MAGIC = 0x5f4c4146; // '_LAF'
	static const ResourceType TYPE;

	enum class CurveType : u8 {
		KEYFRAMED,
		SAMPLED
	};

	enum class Version : u32 {
		FIRST = 3,

		LAST
	};

	struct Header {
		u32 magic;
		Version version;
		Time length;
		u32 frame_count;
	};

	Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	Vec3 getTranslation(Time time, u32 curve_idx) const;
	Quat getRotation(Time time, u32 curve_idx) const;
	int getTranslationCurveIndex(BoneNameHash name_hash) const;
	int getRotationCurveIndex(BoneNameHash name_hash) const;
	void getRelativePose(Time time, Pose& pose, const Model& model, const BoneMask* mask) const;
	void getRelativePose(Time time, Pose& pose, const Model& model, float weight, const BoneMask* mask) const;
	Time getLength() const { return m_length; }

private:
	void unload() override;
	bool load(u64 size, const u8* mem) override;

	struct TranslationCurve
	{
		BoneNameHash name;
		u32 count;
		const u16* times;
		const Vec3* pos;
	};

	struct RotationCurve
	{
		BoneNameHash name;
		u32 count;
		const u16* times;
		const Quat* rot;
	};

	TagAllocator m_allocator;
	Time m_length;
	Array<TranslationCurve> m_translations;
	Array<RotationCurve> m_rotations;
	Array<u8> m_mem;
	u32 m_frame_count = 0;

	friend struct AnimationSampler;
};


inline Time lerp(Time op1, Time op2, float t) {
	const double d = double(op1.raw()) * (1 - t) + double(op2.raw()) * t;
	return Time(u32(d));
}

} // namespace Lumix
