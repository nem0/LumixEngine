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

struct WAVHeader {
	u32 riff;
	u32 chunk_size;
	u32 wave;
	u32 fmt;
	u32 subchunk_size;
	u16 format;
	u16 channels;
	u32 frequency;
	u32 bytes_per_sec;
	u16 align;
	u16 bits_per_sample;
	u32 subchunk2_ID;
	u32 data_size;
}; 

bool Clip::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();
	InputMemoryStream blob(mem, size);
	const u32 version = blob.read<u32>();
	if (version != 0) return false;

	const Format format = blob.read<Format>();
	m_looped = blob.read<bool>();
	m_volume = blob.read<float>();
	switch(format) {
		default: ASSERT(false); return false;
		case Format::WAV: {
			WAVHeader header = blob.read<WAVHeader>();
			if (header.riff != 'FFIR') return false;
			if (header.wave != 'EVAW') return false;
			if (header.bits_per_sample != 16) return false;
			m_channels = header.channels;
			m_sample_rate = header.frequency;
			m_data.resize(u32(blob.size() - blob.getPosition()) / (header.bits_per_sample / 8));
			return blob.read(m_data.begin(), m_data.byte_size());
		}
		case Format::OGG: {
			short* output = nullptr;
			auto res = stb_vorbis_decode_memory((unsigned char*)blob.skip(0), (int)(size - blob.getPosition()), &m_channels, &m_sample_rate, &output);
			if (res <= 0) return false;

			m_data.resize(res * m_channels);
			memcpy(&m_data[0], output, res * m_channels * sizeof(m_data[0]));
			free(output);
			return true;
		}
	}
}


} // namespace Lumix
