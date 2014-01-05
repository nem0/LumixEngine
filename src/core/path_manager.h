#pragma once


#include "core/lux.h"


namespace Lux
{
namespace MT
{
	class Mutex;
}

namespace FS
{
	struct PathString;

	class PathManager
	{
		public:
			static PathManager* create();
			static void destroy(PathManager& manager);

			virtual PathString* addReference(const char* path, uint32_t hash) = 0;
			virtual PathString* addReference(PathString& path_string) = 0;
			virtual void removeReference(PathString& path_string) = 0;

		protected:
			PathManager() {}
			~PathManager() {}
	};
	

} // ~namespace Path
} // ~namspace Lux
