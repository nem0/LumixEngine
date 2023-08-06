#pragma once


#include "engine/array.h"
#include "engine/resource.h"


namespace Lumix {


struct Clip final : Resource
{
	enum class Format : u8 {
		OGG,
		WAV
	};

	Clip(const Path& path, ResourceManager& manager, IAllocator& allocator)
		: Resource(path, manager, allocator)
		, m_data(allocator)
	{
	}

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	int getChannels() const { return m_channels; }
	int getSampleRate() const { return m_sample_rate; }
	int getSize() const { return m_data.size() * sizeof(m_data[0]); }
	u16* getData() { return m_data.begin(); }
	float getLengthSeconds() const { return m_data.size() / float(m_channels * m_sample_rate); }

	static const ResourceType TYPE;
	bool m_looped = false;
	float m_volume = 1;

private:
	int m_channels;
	int m_sample_rate;
	Array<u16> m_data;
};


} // namespace Lumix
