#pragma once

#include "core/resource_manager_base.h"

namespace Lumix
{
	class LUMIX_ENGINE_API ModelManager : public ResourceManagerBase
	{
	public:
		ModelManager() : ResourceManagerBase() {}
		~ModelManager() {}

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;
	};
}