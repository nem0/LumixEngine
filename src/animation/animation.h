#pragma once

#include "engine/resource.h"
#include "engine/resource_manager_base.h"

namespace Lumix
{

namespace FS
{
	class FileSystem;
	class IFile;
}

class Model;
struct Pose;
struct Quat;
struct Vec3;


class AnimationManager : public ResourceManagerBase
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


class Animation : public Resource
{
	public:
		static const uint32 HEADER_MAGIC = 0x5f4c4146; // '_LAF'

	public:
		struct Header
		{
			uint32 magic;
			uint32 version;
			uint32 fps;
		};

	public:
		Animation(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
		~Animation();

		void getRelativePose(float time, Pose& pose, Model& model) const;
		void getRelativePose(float time, Pose& pose, Model& model, float weight) const;
		int getFrameCount() const { return m_frame_count; }
		float getLength() const { return m_frame_count / (float)m_fps; }
		int getFPS() const { return m_fps; }

	private:
		IAllocator& getAllocator();

		void unload() override;
		bool load(FS::IFile& file) override;

	private:
		int	m_frame_count;
		int	m_bone_count;
		Vec3* m_positions;
		Quat* m_rotations;
		uint32* m_bones;
		int m_fps;
};


} // namespace Lumix
