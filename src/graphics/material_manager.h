#pragma once

#include "core/resource_manager_base.h"

namespace Lux
{
	class LUX_ENGINE_API MaterialManager : public ResourceManagerBase
	{
	public:
		MaterialManager() : ResourceManagerBase() {}
		~MaterialManager() {}

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;
	};
}