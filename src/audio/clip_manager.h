#pragma once


#include "core/array.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"


namespace Lumix
{


namespace Audio
{


class LUMIX_LIBRARY_EXPORT Clip : public Resource
{
public:
	Clip(const Path& path, ResourceManager& manager, IAllocator& allocator)
		: Resource(path, manager, allocator)
		, m_data(allocator)
	{
	}

	void unload(void) override;
	bool load(FS::IFile& file) override;
	int getChannels() const { return m_channels; }
	int getSamplerate() const { return m_sample_rate; }
	uint16* getData() { return &m_data[0]; }

private:
	int m_channels;
	int m_sample_rate;
	Array<uint16> m_data;
};


class LUMIX_LIBRARY_EXPORT ClipManager : public ResourceManagerBase
{
public:
	ClipManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{
	}

	~ClipManager() {}

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


} // namespace Audio


} // namespace Lumix
