#include "clip.h"
#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/lumix.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/string.h"
#include "engine/stream.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.cpp"


namespace Lumix
{


const ResourceType Clip::TYPE("clip");


void Clip::unload()
{
	m_data.clear();
}


bool Clip::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();
	InputMemoryStream blob(mem, size);
	const u32 version = blob.read<u32>();
	if (version != 0) return false;

	const Format format = blob.read<Format>();
	if (format != Format::OGG) return false;

	m_looped = blob.read<bool>();
	m_volume = blob.read<float>();

	short* output = nullptr;
	auto res = stb_vorbis_decode_memory((unsigned char*)blob.skip(0), (int)(size - blob.getPosition()), &m_channels, &m_sample_rate, &output);
	if (res <= 0) return false;

	m_data.resize(res * m_channels);
	memcpy(&m_data[0], output, res * m_channels * sizeof(m_data[0]));
	free(output);

	return true;
}


} // namespace Lumix
