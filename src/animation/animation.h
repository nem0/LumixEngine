#pragma once

#include "core/resource.h"
#include "core/resource_manager_base.h"

namespace Lumix
{

namespace FS
{
	class FileSystem;
	class IFile;
}

class Model;
class Pose;
struct Quat;
struct Vec3;


class LUMIX_ANIMATION_API AnimationManager : public ResourceManagerBase
{
public:
	AnimationManager() : ResourceManagerBase() {}
	~AnimationManager() {}

protected:
	virtual Resource* createResource(const Path& path) override;
	virtual void destroyResource(Resource& resource) override;
};


class LUMIX_ANIMATION_API Animation : public Resource
{
	public:
		Animation(const Path& path, ResourceManager& resource_manager);
		~Animation();

		void getPose(float time, Pose& pose, Model& model) const;
		int getFrameCount() const { return m_frame_count; }
		float getLength() const { return m_frame_count / 30.0f; }

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);

		virtual void doUnload(void) override;
		virtual FS::ReadCallback getReadCallback(void) override;

	private:
		int	m_frame_count;
		int	m_bone_count;
		Vec3* m_positions;
		Quat* m_rotations;
		uint32_t* m_bones;
};


} // ~ namespace Lumix
