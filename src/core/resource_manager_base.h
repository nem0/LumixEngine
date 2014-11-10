#pragma once

#include "core/pod_hash_map.h"

namespace Lumix 
{
	namespace FS
	{
		class FileSystem;
	}

	class Path;
	class Resource;
	class ResourceManager;

	class LUMIX_CORE_API ResourceManagerBase abstract
	{
		friend class Resource;
		typedef PODHashMap<uint32_t, Resource*> ResourceTable;

	public:
		void create(uint32_t id, ResourceManager& owner);
		void destroy(void);

		Resource* get(const Path& path);
		Resource* load(const Path& path);
		void load(Resource& resource);

		void unload(const Path& path);
		void unload(Resource& resource);

		void forceUnload(const Path& path);
		void forceUnload(Resource& resource);

		void reload(const Path& path);
		void reload(Resource& resource);

		ResourceManagerBase(IAllocator& allocator);
		~ResourceManagerBase(void);

	protected:
		virtual Resource* createResource(const Path& path) = 0;
		virtual void destroyResource(Resource& resource) = 0;

		ResourceManager& getOwner() const { return *m_owner; }
	private:

		uint32_t m_size;
		ResourceTable m_resources;
		ResourceManager* m_owner;
	};
}