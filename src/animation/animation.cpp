#include "animation/animation.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/vec3.h"
#include "graphics/model.h"
#include "graphics/pose.h"


namespace Lumix
{


static const uint32_t ANIMATION_HEADER_MAGIC = 0x5f4c4146; // '_LAF'


struct AnimationHeader
{
	uint32_t magic;
	uint32_t version;
};


Resource* AnimationManager::createResource(const Path& path)
{
	return LUMIX_NEW(Animation)(path, getOwner());
}


void AnimationManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(static_cast<Animation*>(&resource));
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
	LUMIX_DELETE_ARRAY(m_positions);
	LUMIX_DELETE_ARRAY(m_rotations);
}


static const float ANIMATION_FPS = 30.0f;


void Animation::getPose(float time, Pose& pose, Model& model) const
{
	if(model.isReady())
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
				int parent = model.getBone(i).parent_idx;
				if (parent >= 0)
				{
					pos[i] = rot[parent] * pos[i] + pos[parent];
					rot[i] = rot[i] * rot[parent];
				}
			}
		}
		else
		{
			for(int i = 0; i < m_bone_count; ++i)
			{
				pos[i] = m_positions[off + i];
				rot[i] = m_rotations[off + i];
				int parent = model.getBone(i).parent_idx;
				if (parent >= 0)
				{
					pos[i] = rot[parent] * pos[i] + pos[parent];
					rot[i] = rot[i] * rot[parent];
				}
			}
		}
	}
}


void Animation::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if (success)
	{
		LUMIX_DELETE_ARRAY(m_positions);
		LUMIX_DELETE_ARRAY(m_rotations);
		m_positions = NULL;
		m_rotations = NULL;
		m_frame_count = m_bone_count = 0;
		AnimationHeader header;
		file->read(&header, sizeof(header));
		if (header.magic != ANIMATION_HEADER_MAGIC)
		{
			fs.close(file);
			onFailure();
			g_log_error.log("animation") << m_path.c_str() << " is not an animation file";
			return;
		}
		if (header.version > 1)
		{
			fs.close(file);
			onFailure();
			g_log_error.log("animation") << "Unsupported animation version " << header.version << " (" << m_path.c_str() << ")";
			return;
		}
		file->read(&m_frame_count, sizeof(m_frame_count));
		file->read(&m_bone_count, sizeof(m_bone_count));

		m_positions = LUMIX_NEW_ARRAY(Vec3, m_frame_count * m_bone_count);
		m_rotations = LUMIX_NEW_ARRAY(Quat, m_frame_count * m_bone_count);
		file->read(&m_positions[0], sizeof(Vec3)* m_bone_count * m_frame_count);
		file->read(&m_rotations[0], sizeof(Quat)* m_bone_count * m_frame_count);

		m_size = file->size();
		decrementDepCount();
	}
	else
	{
		onFailure();
	}

	fs.close(file);
}


void Animation::doUnload(void)
{
	LUMIX_DELETE_ARRAY(m_positions);
	LUMIX_DELETE_ARRAY(m_rotations);
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

} // ~namespace Lumix
