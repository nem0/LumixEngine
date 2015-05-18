#pragma once

#include "core/pod_hash_map.h"

namespace Lumix
{
	class Resource;
	namespace FS
	{
		class FileSystem;
	}

	class ResourceManagerBase;

	class LUMIX_CORE_API ResourceManager final
	{
		typedef PODHashMap<uint32_t, ResourceManagerBase*> ResourceManagerTable;
	public:
		static const uint32_t MATERIAL		= 0xba8de9d9; //MATERIAL
		static const uint32_t MODEL			= 0x06991edf; //MODEL
		static const uint32_t SHADER		= 0x0f0b59ae; //SHADER
		static const uint32_t TEXTURE		= 0xbd23f368; //TEXTURE
		static const uint32_t PIPELINE		= 0xbbcf4595; //PIPELINE
		static const uint32_t ANIMATION		= 0xc9909a33; //ANIMATION
		static const uint32_t BITMAP_FONT	= 0x89DEEEB4; //BITMAP_FONT
		static const uint32_t PHYSICS		= 0xE77419F9; //PHYSICS

		ResourceManager(IAllocator& allocator);
		~ResourceManager();

		void create(FS::FileSystem& fs);
		void destroy(void);

		IAllocator& getAllocator() { return m_allocator; }
		ResourceManagerBase* get(uint32_t id);

		void add(uint32_t id, ResourceManagerBase* rm);
		void remove(uint32_t id);
		void reload(const char* path);
		bool isLoading() const;
		void incrementLoadingResources();
		void decrementLoadingResources();

		FS::FileSystem& getFileSystem() { return *m_file_system; }

	private:
		IAllocator& m_allocator;
		ResourceManagerTable m_resource_managers;
		FS::FileSystem* m_file_system;
		int m_loading_resources_count;
	};
}