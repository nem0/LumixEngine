#include "animation/animation.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/quat.h"
#include "core/vec3.h"
#include "graphics/pose.h"


namespace Lux
{


Animation::Animation()
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


void Animation::load(const char* filename, FS::FileSystem& file_system)
{
	FS::ReadCallback cb;
	cb.bind<Animation, &Animation::loaded>(this);
	file_system.openAsync(file_system.getDefaultDevice(), filename, FS::Mode::OPEN | FS::Mode::READ, cb);
}


void Animation::loaded(FS::IFile* file, bool success)
{
	LUX_DELETE_ARRAY(m_positions);
	LUX_DELETE_ARRAY(m_rotations);
	file->read(&m_frame_count, sizeof(m_frame_count));
	file->read(&m_bone_count, sizeof(m_bone_count));
	m_positions = new Vec3[m_frame_count * m_bone_count];
	m_rotations = new Quat[m_frame_count * m_bone_count];
	for(int i = 0; i < m_frame_count; ++i)
	{
		for(int j = 0; j < m_bone_count; ++j)
		{
			/// TODO positions (rotations) in a row
			file->read(&m_positions[i * m_bone_count + j], sizeof(Vec3));
			file->read(&m_rotations[i * m_bone_count + j], sizeof(Quat));
		}
	}

	/// TODO close file somehow
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


} // ~namespace Lux
