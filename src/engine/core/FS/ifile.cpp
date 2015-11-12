#include "ifile.h"
#include "core/string.h"
#include "ifile_device.h"

namespace Lumix
{
	namespace FS
	{

		void IFile::release()
		{
			getDevice().destroyFile(this);
		}


		IFile& IFile::operator<<(const char* text)
		{
			write(text, stringLength(text));
			return *this;
		}

	}
}