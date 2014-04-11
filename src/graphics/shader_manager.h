#pragma once

#include "core/resource_manager_base.h"

namespace Lux
{
	class LUX_ENGINE_API ShaderManager : public ResourceManagerBase
	{
	public:
		ShaderManager() : ResourceManagerBase() {}
		~ShaderManager() {}

	protected:
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;
	};
}