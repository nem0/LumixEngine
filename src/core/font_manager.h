#pragma once

#include "core/resource_manager_base.h"

namespace Lux
{
	class LUX_CORE_API FontManager : public ResourceManagerBase
	{
	public:
		FontManager() : ResourceManagerBase() {}
		~FontManager() {}

		void create(FS::FileSystem& fs);

	protected:
		virtual Resource* createResource(const Path& path) LUX_OVERRIDE;
		virtual void destroyResource(Resource& resource) LUX_OVERRIDE;
	};
}