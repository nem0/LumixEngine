#pragma once


#include "path.h"


namespace Lumix
{


class LUMIX_ENGINE_API Library
{
	public:
		static Library* create(const Path& path, class IAllocator& allocator);
		static void destroy(Library* library);

		virtual ~Library() {}

		virtual bool isLoaded() const = 0;
		virtual bool load() = 0;
		virtual bool unload() = 0;
		virtual void* resolve(const char* name) = 0;
};


} // namespace Lumix