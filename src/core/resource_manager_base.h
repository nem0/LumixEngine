#pragma once

#include "core/pod_hash_map.h"

//class ResourceBase()
//{
//		ResourceBase();
//	~ResourceBase();
//};

namespace Lux 
{
	namespace FS
	{
		class FileSystem;
	}

	class Path;
	class Resource;

	class LUX_CORE_API ResourceManagerBase LUX_ABSTRACT
	{
	public:
		void create(FS::FileSystem& fs);
		void destroy(void);

		Resource* get(const Path& path);
		Resource* load(const Path& path);

		void unload(const Path& path);
		void unload(Resource& resource);

		void forceUnload(const Path& path);
		void forceUnload(Resource& resource);

		void reload(const Path& path);
		void reload(Resource& resource);

		void releaseAll(void);

		ResourceManagerBase(void);
		~ResourceManagerBase(void);
	protected:
		virtual Resource* createResource(const Path& path) = 0;
		virtual void destroyResource(Resource& resource) = 0;

	private:
		typedef PODHashMap<uint32_t, Resource*> ResourceTable;

		uint32_t m_size;
		ResourceTable m_resources;
		FS::FileSystem* m_file_system;
	};
}