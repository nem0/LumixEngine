#pragma once

#include "core/pod_hash_map.h"

namespace Lux
{
	namespace FS
	{
		class FileSystem;
	}

	class ResourceManagerBase;

	class LUX_CORE_API ResourceManager LUX_FINAL
	{
		typedef PODHashMap<uint32_t, ResourceManagerBase*> ResourceManagerTable;
	public:
		static const uint32_t MATERIAL_MANAGER = 0xd55d4e03; //MATERIAL_MANAGER

		ResourceManager();
		~ResourceManager();

		void create(FS::FileSystem& fs);
		void destroy(void);

		ResourceManagerBase* get(uint32_t id);

		void add(uint32_t id, ResourceManagerBase* rm);
		void remove(uint32_t id);

		FS::FileSystem& getFileSystem() { return *m_file_system; }

	private:
		ResourceManagerTable m_resource_managers;
		FS::FileSystem* m_file_system;
	};
}