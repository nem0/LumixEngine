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
		static const uint32_t MATERIAL	= 0xba8de9d9; //MATERIAL
		static const uint32_t MODEL		= 0x06991edf; //MODEL
		static const uint32_t SHADER	= 0x0f0b59ae; //SHADER
		static const uint32_t TEXTURE	= 0xbd23f368; //TEXTURE

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