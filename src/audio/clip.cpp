#include "clip.h"
#include "engine/allocator.h"
#include "engine/lumix.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/string.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.cpp"
#include <cstdlib>


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
	short* output = nullptr;
	auto res = stb_vorbis_decode_memory((unsigned char*)mem, (int)size, &m_channels, &m_sample_rate, &output);
	if (res <= 0) return false;

	m_data.resize(res * m_channels);
	copyMemory(&m_data[0], output, res * m_channels * sizeof(m_data[0]));
	free(output);

	return true;
}


} // namespace Lumix
