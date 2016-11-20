#pragma once


#include "engine/array.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


class Clip LUMIX_FINAL : public Resource
{
public:
	Clip(const Path& path, ResourceManagerBase& manager, IAllocator& allocator)
		: Resource(path, manager, allocator)
		, m_data(allocator)
	{
	}

	void unload(void) override;
	bool load(FS::IFile& file) override;
	int getChannels() const { return m_channels; }
	int getSampleRate() const { return m_sample_rate; }
	int getSize() const { return m_data.size() * sizeof(m_data[0]); }
	u16* getData() { return &m_data[0]; }
	float getLengthSeconds() const { return m_data.size() / float(m_channels * m_sample_rate); }

private:
	int m_channels;
	int m_sample_rate;
	Array<u16> m_data;
};


class ClipManager LUMIX_FINAL : public ResourceManagerBase
{
public:
	explicit ClipManager(IAllocator& allocator)
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


} // namespace Lumix
