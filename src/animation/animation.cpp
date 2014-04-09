#include "animation/animation.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/quat.h"
#include "core/vec3.h"
#include "graphics/pose.h"


namespace Lux
{


Resource* AnimationManager::createResource(const Path& path)
{
	return LUX_NEW(Animation)(path, getOwner());
}


void AnimationManager::destroyResource(Resource& resource)
{
	LUX_DELETE(static_cast<Animation*>(&resource));
}


Animation::Animation(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
{
	m_rotations = NULL;
	m_positions = NULL;
	m_frame_count = 0;
}


Animation::~Animation()
{
	LUX_DELETE_ARRAY(m_positions);
	LUX_DELETE_ARRAY(m_rotations);
}


static const float ANIMATION_FPS = 30.0f;


void Animation::getPose(float time, Pose& pose) const
{
	int frame = (int)(time * ANIMATION_FPS);
	frame = frame >= m_frame_count ? m_frame_count - 1 : frame;
	Vec3* pos = pose.getPositions();
	Quat* rot = pose.getRotations();
	int off = frame * m_bone_count;
	int off2 = off + m_bone_count;
	float t = (time - frame / ANIMATION_FPS) / (1 / ANIMATION_FPS);
	
	if(frame < m_frame_count - 1)
	{
		for(int i = 0; i < m_bone_count; ++i)
		{
			lerp(m_positions[off + i], m_positions[off2 + i], &pos[i], t);
			nlerp(m_rotations[off + i], m_rotations[off2 + i], &rot[i], t);
		}
	}
	else
	{
		for(int i = 0; i < m_bone_count; ++i)
		{
			pos[i] = m_positions[off + i];
			rot[i] = m_rotations[off + i];
		}
	}
}


void Animation::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if (success)
	{
		TODO("Add same header to the animation file and check it.");
		LUX_DELETE_ARRAY(m_positions);
		LUX_DELETE_ARRAY(m_rotations);
		file->read(&m_frame_count, sizeof(m_frame_count));
		file->read(&m_bone_count, sizeof(m_bone_count));
		m_positions = LUX_NEW_ARRAY(Vec3, m_frame_count * m_bone_count);
		m_rotations = LUX_NEW_ARRAY(Quat, m_frame_count * m_bone_count);
		for (int i = 0; i < m_frame_count; ++i)
		{
			for (int j = 0; j < m_bone_count; ++j)
			{
				TODO("positions(rotations) in a row");
				file->read(&m_positions[i * m_bone_count + j], sizeof(Vec3));
				file->read(&m_rotations[i * m_bone_count + j], sizeof(Quat));
			}
		}

		m_size = file->size();
		onReady();
	}
	else
	{
		onFailure();
	}

	fs.close(file);
}


void Animation::doUnload(void)
{
	LUX_DELETE_ARRAY(m_positions);
	LUX_DELETE_ARRAY(m_rotations);
	m_rotations = NULL;
	m_positions = NULL;
	m_frame_count = 0;
	m_size = 0;
	onEmpty();
}


FS::ReadCallback Animation::getReadCallback(void)
{
	FS::ReadCallback cb;
	cb.bind<Animation, &Animation::loaded>(this);
	return cb;
}

} // ~namespace Lux
