#include "core/allocator.h"
#include "core/core.h"
#include "core/crt.h"
#include "core/profiler.h"
#include "core/string.h"
#include "core/stream.h"

#include "clip.h"
#include "engine/resource.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.cpp"


namespace black
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
}; 

struct WAVChunk {
	u32 type;
	u32 size;
};

bool Clip::load(Span<const u8> mem) {
	PROFILE_FUNCTION();
	InputMemoryStream blob(mem);
	const u32 version = blob.read<u32>();
	if (version != 0) return false;

	const Format format = blob.read<Format>();
	m_looped = blob.read<bool>();
	m_volume = blob.read<float>();
	switch(format) {
		case Format::WAV: {
			WAVHeader header = blob.read<WAVHeader>();
			if (header.riff != 'FFIR') return false;
			if (header.wave != 'EVAW') return false;
			if (header.bits_per_sample != 16) return false;
			blob.skip(header.subchunk_size - 16);
			m_channels = header.channels;
			m_sample_rate = header.frequency;

			for (;;) {
				const WAVChunk chunk = blob.read<WAVChunk>();
				if (chunk.type == 'atad') {
					m_data.resize(u32(chunk.size / sizeof(m_data[0])));
					return blob.read(m_data.begin(), m_data.byte_size());
				}
				blob.skip(chunk.size);
				if (blob.getPosition() >= blob.size()) return false;
			}
		}
		case Format::OGG: {
			PROFILE_BLOCK("ogg");
			short* output = nullptr;
			auto res = stb_vorbis_decode_memory((unsigned char*)blob.skip(0), (int)blob.remaining(), &m_channels, &m_sample_rate, &output);
			if (res <= 0) return false;

			m_data.resize(res * m_channels);
			memcpy(&m_data[0], output, res * m_channels * sizeof(m_data[0]));
			free(output);
			return true;
		}
	}
	ASSERT(false);
	return false;
}


} // namespace black
