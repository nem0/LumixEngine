#include "ifile.h"
#include "ifile_device.h"

namespace Lumix
{
	namespace FS
	{

		void IFile::release()
		{
			getDevice().destroyFile(this);
		}

	}
}