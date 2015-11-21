#include "clip_manager.h"
#include "core/iallocator.h"
#include "core/fs/ifile.h"
#include "core/resource.h"
#include "lumix.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
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

	m_data.resize(res);
	copyMemory(&m_data[0], output, res);
	free(output);

	return true;
}


Resource* ClipManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, Clip)(path, getOwner(), m_allocator);
}


void ClipManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<Clip*>(&resource));
}


} // namespace Lumix