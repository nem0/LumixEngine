#include "engine/crt.h"
#include "engine/hash.h"
#include "prefab.h"

namespace Lumix
{


const ResourceType PrefabResource::TYPE("prefab");


PrefabResource::PrefabResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, data(allocator)
{
}


ResourceType PrefabResource::getType() const { return TYPE; }


void PrefabResource::unload() { data.clear(); }


bool PrefabResource::load(Span<const u8> blob)
{
	data.resize((int)blob.length());
	memcpy(data.getMutableData(), blob.begin(), blob.length());
	content_hash = StableHash(blob.begin(), (u32)blob.length());
	return true;
}


}