#include "clip_manager.h"
#include "engine/iallocator.h"
#include "engine/resource.h"
#include "engine/string.h"
#include "engine/lumix.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.cpp"
#include <cstdlib>


namespace Lumix
{


void Clip::unload()
{
	m_data.clear();
}


bool Clip::load(FS::IFile& file)
{
	short* output = nullptr;
	auto res = stb_vorbis_decode_memory(
		(unsigned char*)file.getBuffer(), (int)file.size(), &m_channels, &m_sample_rate, &output);
	if (res <= 0) return false;

	m_data.resize(res * m_channels);
	copyMemory(&m_data[0], output, res * m_channels * sizeof(m_data[0]));
	free(output);

	return true;
}


Resource* ClipManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, Clip)(path, *this, m_allocator);
}


void ClipManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<Clip*>(&resource));
}


} // namespace Lumix
