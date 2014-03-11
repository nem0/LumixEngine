#pragma once


#include <Windows.h>
#include <gl/GL.h>
#include "core/string.h"
#include "core/resource.h"

namespace Lux
{
namespace FS
{
	class FileSystem;
}


class Texture : public Resource
{
	public:
		Texture(const Path& path, ResourceManager& resource_manager);
		~Texture();

		bool create(int w, int h);
		void apply(int unit = 0);

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);

		virtual void doUnload(void) LUX_OVERRIDE;
		virtual FS::ReadCallback getReadCallback() LUX_OVERRIDE;

	private:
		GLuint m_id;
};


} // ~namespace Lux
