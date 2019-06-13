#pragma once

#include "engine/math.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"

namespace Lumix
{

class Model;
struct Pose;
struct Quat;
struct Vec3;


class AnimationManager final : public ResourceManager
{
public:
	explicit AnimationManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator)
	{}
	~AnimationManager() {}
	IAllocator& getAllocator() { return m_allocator; }

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


struct BoneMask
{
	BoneMask(IAllocator& allocator) : bones(allocator) {}
	u32 name;
	HashMap<u32, u8> bones;
};


class Animation final : public Resource
{
	public:
		static const u32 HEADER_MAGIC = 0x5f4c4146; // '_LAF'
		static const ResourceType TYPE;

	public:
		struct Header
		{
			u32 magic;
			u32 version;
			u32 fps;
		};

	public:
		Animation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

		ResourceType getType() const override { return TYPE; }

		int getRootMotionBoneIdx() const { return m_root_motion_bone_idx; }
		LocalRigidTransform getBoneTransform(float time, int bone_idx) const;
		void getRelativePose(float time, Pose& pose, Model& model, BoneMask* mask) const;
		void getRelativePose(float time, Pose& pose, Model& model, float weight, BoneMask* mask) const;
		int getFrameCount() const { return m_frame_count; }
		float getLength() const { return m_frame_count / (float)m_fps; }
		int getFPS() const { return m_fps; }
		int getBoneCount() const { return m_bones.size(); }
		int getBoneIndex(u32 name) const;

	private:
		IAllocator& getAllocator() const;

		void unload() override;
		bool load(u64 size, const u8* mem) override;

	private:
		int	m_frame_count;
		struct Bone
		{
			u32 name;
			int pos_count;
			const u16* pos_times;
			const Vec3* pos;
			int rot_count;
			const u16* rot_times;
			const Quat* rot;
		};
		Array<Bone> m_bones;
		Array<u8> m_mem;
		int m_fps;
		int m_root_motion_bone_idx;
};


} // namespace Lumix
