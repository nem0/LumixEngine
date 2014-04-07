#pragma once

#include "core/resource.h"
#include "core/resource_manager_base.h"

namespace Lux
{

namespace FS
{
	class FileSystem;
	class IFile;
}

struct Vec3;
struct Quat;
class Pose;


class LUX_ENGINE_API AnimationManager : public ResourceManagerBase
{
public:
	AnimationManager() : ResourceManagerBase() {}
	~AnimationManager() {}

protected:
	virtual Resource* createResource(const Path& path) LUX_OVERRIDE;
	virtual void destroyResource(Resource& resource) LUX_OVERRIDE;
};


class Animation : public Resource
{
	public:
		Animation(const Path& path, ResourceManager& resource_manager);
		~Animation();

		void getPose(float time, Pose& pose) const;
		int getFrameCount() const { return m_frame_count; }
		float getLength() const { return m_frame_count / 30.0f; }

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);

		virtual void doUnload(void) LUX_OVERRIDE;
		virtual FS::ReadCallback getReadCallback(void) LUX_OVERRIDE;

	private:
		int	m_frame_count;
		int	m_bone_count;
		Vec3* m_positions;
		Quat* m_rotations;
};


} // ~ namespace Lux
