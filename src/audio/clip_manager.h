#pragma once


#include "core/array.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"


namespace Lumix
{


class LUMIX_AUDIO_API Clip : public Resource
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
	int getSampleRate() const { return m_sample_rate; }
	int getSize() const { return m_data.size() * sizeof(m_data[0]); }
	uint16* getData() { return &m_data[0]; }
	float getLengthSeconds() const { return m_data.size() / float(m_channels * m_sample_rate); }

private:
	int m_channels;
	int m_sample_rate;
	Array<uint16> m_data;
};


class LUMIX_AUDIO_API ClipManager : public ResourceManagerBase
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


} // namespace Lumix
