#pragma once


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


class Animation
{
	public:
		Animation();
		~Animation();

		void load(const char* filename, FS::FileSystem& file_system);
		void getPose(float time, Pose& pose) const;
		int getFrameCount() const { return m_frame_count; }
		float getLength() const { return m_frame_count / 30.0f; }

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);

	private:
		int	m_frame_count;
		int	m_bone_count;
		Vec3* m_positions;
		Quat* m_rotations;
};


} // ~ namespace Lux
