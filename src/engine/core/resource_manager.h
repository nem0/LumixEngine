#pragma once


#include "core/hash_map.h"


namespace Lumix
{


class Path;
class Resource;


namespace FS
{
class FileSystem;
}


class ResourceManagerBase;


class LUMIX_ENGINE_API ResourceManager final
{
	typedef HashMap<uint32, ResourceManagerBase*> ResourceManagerTable;

public:
	static const uint32 MATERIAL = 0xba8de9d9; // MATERIAL
	static const uint32 MODEL = 0x06991edf; // MODEL
	static const uint32 SHADER = 0x0f0b59ae; // SHADER
	static const uint32 TEXTURE = 0xbd23f368; // TEXTURE
	static const uint32 ANIMATION = 0xc9909a33; // ANIMATION
	static const uint32 PHYSICS = 0xE77419F9; // PHYSICS
	static const uint32 FILE = 0xBA0ADBA4; // FILE
	static const uint32 SHADER_BINARY = 0xDC8D194B; // SHADER_BINARY

	explicit ResourceManager(IAllocator& allocator);
	~ResourceManager();

	void create(FS::FileSystem& fs);
	void destroy();

	IAllocator& getAllocator() { return m_allocator; }
	ResourceManagerBase* get(uint32 id);
	const ResourceManagerTable& getAll() const { return m_resource_managers; }

	void add(uint32 id, ResourceManagerBase* rm);
	void remove(uint32 id);
	void reload(const Path& path);
	void removeUnreferenced();

	FS::FileSystem& getFileSystem() { return *m_file_system; }

private:
	IAllocator& m_allocator;
	ResourceManagerTable m_resource_managers;
	FS::FileSystem* m_file_system;
};


} // namespace Lumix