#pragma once

#include "engine/matrix.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"

namespace Lumix
{

namespace FS
{
	class FileSystem;
	struct IFile;
}

class Model;
struct Pose;
struct Quat;
struct Vec3;


class AnimationManager LUMIX_FINAL : public ResourceManagerBase
{
public:
	explicit AnimationManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
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


class Animation LUMIX_FINAL : public Resource
{
	public:
		static const u32 HEADER_MAGIC = 0x5f4c4146; // '_LAF'

	public:
		struct Header
		{
			u32 magic;
			u32 version;
			u32 fps;
		};

	public:
		Animation(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);

		int getRootMotionBoneIdx() const { return m_root_motion_bone_idx; }
		RigidTransform getBoneTransform(float time, int bone_idx) const;
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
		bool load(FS::IFile& file) override;

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
