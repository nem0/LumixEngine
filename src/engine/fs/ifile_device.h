#pragma once

#include "engine/lumix.h"

namespace Lumix
{
namespace FS
{


struct IFile;


struct LUMIX_ENGINE_API IFileDevice
{
	IFileDevice() {}
	virtual ~IFileDevice() {}

	virtual IFile* createFile() = 0;
	virtual void destroyFile(IFile* file) = 0;
};


} // namespace FS
} // namespace Lumix
