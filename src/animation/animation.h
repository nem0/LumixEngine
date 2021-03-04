#pragma once

#include "engine/hash_map.h"
#include "engine/resource.h"
#include "engine/string.h"

namespace Lumix
{

struct Model;
struct Pose;
struct Quat;
struct Vec3;


struct BoneMask
{
	BoneMask(IAllocator& allocator) : bones(allocator) {}
	BoneMask(BoneMask&& rhs) = default;
	StaticString<32> name;
	HashMap<u32, u8, HashFuncDirect<u32>> bones;
};


struct Animation final : Resource
{
	public:
		static const u32 HEADER_MAGIC = 0x5f4c4146; // '_LAF'
		static const ResourceType TYPE;

	public:
		enum class CurveType : u8 {
			KEYFRAMED,
			SAMPLED
		};

		struct Header
		{
			u32 magic;
			u32 version;
			Time length;
			u32 frame_count;
		};

	public:
		Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

		ResourceType getType() const override { return TYPE; }

		Vec3 getTranslation(Time time, u32 curve_idx) const;
		Quat getRotation(Time time, u32 curve_idx) const;
		int getTranslationCurveIndex(u32 name_hash) const;
		int getRotationCurveIndex(u32 name_hash) const;
		void getRelativePose(Time time, Pose& pose, const Model& model, const BoneMask* mask) const;
		void getRelativePose(Time time, Pose& pose, const Model& model, float weight, const BoneMask* mask) const;
		Time getLength() const { return m_length; }

	private:
		void unload() override;
		bool load(u64 size, const u8* mem) override;

	private:
		Time m_length;
		struct TranslationCurve
		{
			u32 name;
			u32 count;
			const u16* times;
			const Vec3* pos;
		};
		struct RotationCurve
		{
			u32 name;
			u32 count;
			const u16* times;
			const Quat* rot;
		};
		Array<TranslationCurve> m_translations;
		Array<RotationCurve> m_rotations;
		Array<u8> m_mem;
		u32 m_frame_count = 0;
		int m_root_motion_bone_idx;

		friend struct AnimationSampler;
};


} // namespace Lumix
