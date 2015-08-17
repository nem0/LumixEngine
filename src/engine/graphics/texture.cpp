#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/texture.h"
#include "graphics/texture_manager.h"
#include <bgfx.h>
#include <cmath>

namespace Lumix
{


#pragma pack(1)
struct TGAHeader
{
	char idLength;
	char colourMapType;
	char dataType;
	short int colourMapOrigin;
	short int colourMapLength;
	char colourMapDepth;
	short int xOrigin;
	short int yOrigin;
	short int width;
	short int height;
	char bitsPerPixel;
	char imageDescriptor;
};
#pragma pack()


Texture::Texture(const Path& path,
				 ResourceManager& resource_manager,
				 IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_data_reference(0)
	, m_allocator(allocator)
	, m_data(m_allocator)
	, m_BPP(-1)
{
	m_texture_handle = BGFX_INVALID_HANDLE;
}


Texture::~Texture()
{
	ASSERT(isEmpty());
}


void Texture::destroy()
{
	doUnload();
}


bool Texture::create(int w, int h, void* data)
{
	if (data)
	{
		m_texture_handle = bgfx::createTexture2D(w,
												 h,
												 1,
												 bgfx::TextureFormat::RGBA8,
												 0,
												 bgfx::copy(data, w * h * 4));
	}
	else
	{
		m_texture_handle =
			bgfx::createTexture2D(w, h, 1, bgfx::TextureFormat::RGBA8);
	}


	bool isReady = bgfx::isValid(m_texture_handle);

	if (isReady)
	{
		onReady();
	}
	else
	{
		onFailure();
	}

	return isReady;
}


uint32_t Texture::getPixel(float x, float y) const
{
	if (m_data.empty() || x >= m_width || y >= m_height || x < 0 || y < 0)
	{
		return 0;
	}

	// http://fastcpp.blogspot.sk/2011/06/bilinear-pixel-interpolation-using-sse.html
	int px = (int)x;
	int py = (int)y;
	const uint32_t* p0 = (uint32_t*)&m_data[(px + py * m_width) * 4];

	const uint8_t* p1 = (uint8_t*)p0;
	const uint8_t* p2 = (uint8_t*)(p0 + 1);
	const uint8_t* p3 = (uint8_t*)(p0 + m_width);
	const uint8_t* p4 = (uint8_t*)(p0 + 1 + m_width);

	float fx = x - px;
	float fy = y - py;
	float fx1 = 1.0f - fx;
	float fy1 = 1.0f - fy;

	int w1 = (int)(fx1 * fy1 * 256.0f);
	int w2 = (int)(fx * fy1 * 256.0f);
	int w3 = (int)(fx1 * fy * 256.0f);
	int w4 = (int)(fx * fy * 256.0f);

	uint8_t res[4];
	res[0] =
		(uint8_t)((p1[0] * w1 + p2[0] * w2 + p3[0] * w3 + p4[0] * w4) >> 8);
	res[1] =
		(uint8_t)((p1[1] * w1 + p2[1] * w2 + p3[1] * w3 + p4[1] * w4) >> 8);
	res[2] =
		(uint8_t)((p1[2] * w1 + p2[2] * w2 + p3[2] * w3 + p4[2] * w4) >> 8);
	res[3] =
		(uint8_t)((p1[3] * w1 + p2[3] * w2 + p3[3] * w3 + p4[3] * w4) >> 8);

	return *(uint32_t*)res;
}


unsigned int Texture::compareTGA(IAllocator& allocator,
								 FS::IFile* file1,
								 FS::IFile* file2,
								 int difference)
{
	TGAHeader header1, header2;
	file1->read(&header1, sizeof(header1));
	file2->read(&header2, sizeof(header2));

	if (header1.bitsPerPixel != header2.bitsPerPixel ||
		header1.width != header2.width || header1.height != header2.height ||
		header1.dataType != header2.dataType ||
		header1.imageDescriptor != header2.imageDescriptor)
	{
		g_log_error.log("renderer")
			<< "Trying to compare textures with different formats";
		return 0;
	}

	int color_mode = header1.bitsPerPixel / 8;
	if (header1.dataType != 2)
	{
		g_log_error.log("renderer") << "Unsupported texture format";
		return 0;
	}

	// Targa is BGR, swap to RGB, add alpha and flip Y axis
	int different_pixel_count = 0;
	size_t pixel_count = header1.width * header1.height;
	uint8_t* img1 = (uint8_t*)allocator.allocate(pixel_count * color_mode);
	uint8_t* img2 = (uint8_t*)allocator.allocate(pixel_count * color_mode);

	file1->read(img1, pixel_count * color_mode);
	file2->read(img2, pixel_count * color_mode);

	for (size_t i = 0; i < pixel_count * color_mode; i += color_mode)
	{
		for (int j = 0; j < color_mode; ++j)
		{
			if (Math::abs(img1[i + j] - img2[i + j]) > difference)
			{
				++different_pixel_count;
				break;
			}
		}
	}

	allocator.deallocate(img1);
	allocator.deallocate(img2);

	return different_pixel_count;
}


bool Texture::saveTGA(IAllocator& allocator,
					  FS::IFile* file,
					  int width,
					  int height,
					  int bytes_per_pixel,
					  const uint8_t* image_dest,
					  const Path& path)
{
	if (bytes_per_pixel != 4)
	{
		g_log_error.log("renderer")
			<< "Texture " << path.c_str()
			<< " could not be saved, unsupported TGA format";
		return false;
	}

	uint8_t* data = (uint8_t*)allocator.allocate(width * height * 4);

	TGAHeader header;
	memset(&header, 0, sizeof(header));
	header.bitsPerPixel = (char)(bytes_per_pixel * 8);
	header.height = (short)height;
	header.width = (short)width;
	header.dataType = 2;

	file->write(&header, sizeof(header));

	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * 4;
		long write_index = ((header.imageDescriptor & 32) != 0)
							   ? read_index
							   : y * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			data[write_index + 0] = image_dest[write_index + 2];
			data[write_index + 1] = image_dest[write_index + 1];
			data[write_index + 2] = image_dest[write_index + 0];
			data[write_index + 3] = image_dest[write_index + 3];
			write_index += 4;
		}
	}

	file->write(data, width * height * 4);

	allocator.deallocate(data);

	return true;
}


void Texture::saveTGA()
{
	if (m_data.empty())
	{
		g_log_error.log("renderer")
			<< "Texture " << getPath().c_str()
			<< " could not be saved, no data was loaded";
		return;
	}

	FS::FileSystem& fs = m_resource_manager.getFileSystem();
	FS::IFile* file = fs.open(fs.getDiskDevice(),
							  getPath().c_str(),
							  FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);

	saveTGA(m_allocator, file, m_width, m_height, m_BPP, &m_data[0], getPath());

	fs.close(*file);
}


void Texture::save()
{
	char ext[5];
	ext[0] = 0;
	PathUtils::getExtension(ext, 5, getPath().c_str());
	if (strcmp(ext, "raw") == 0 && m_BPP == 2)
	{
		FS::FileSystem& fs = m_resource_manager.getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(),
								  getPath().c_str(),
								  FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);

		file->write(&m_data[0], m_data.size() * sizeof(m_data[0]));
		fs.close(*file);
	}
	else if (strcmp(ext, "tga") == 0 && m_BPP == 4)
	{
		saveTGA();
	}
	else
	{
		g_log_error.log("renderer") << "Texture " << getPath()
									<< " can not be saved - unsupported format";
	}
}


void Texture::onDataUpdated()
{
	const bgfx::Memory* mem = nullptr;

	if (m_BPP == 2)
	{
		const uint16_t* src_mem = (const uint16_t*)&m_data[0];
		mem = bgfx::alloc(m_width * m_height * sizeof(float));
		float* dst_mem = (float*)mem->data;

		for (int i = 0; i < m_width * m_height; ++i)
		{
			dst_mem[i] = src_mem[i] / 65535.0f;
		}
	}
	else
	{
		mem = bgfx::copy(&m_data[0], m_data.size() * sizeof(m_data[0]));
	}
	bgfx::updateTexture2D(m_texture_handle, 0, 0, 0, m_width, m_height, mem);
}


bool Texture::loadRaw(FS::IFile& file)
{
	PROFILE_FUNCTION();
	size_t size = file.size();
	m_BPP = 2;
	m_width = (int)sqrt(size / m_BPP);
	m_height = m_width;

	if (m_data_reference)
	{
		m_data.resize(size);
		file.read(&m_data[0], size);
	}

	const uint16_t* src_mem = (const uint16_t*)file.getBuffer();
	const bgfx::Memory* mem = bgfx::alloc(m_width * m_height * sizeof(float));
	float* dst_mem = (float*)mem->data;

	for (int i = 0; i < m_width * m_height; ++i)
	{
		dst_mem[i] = src_mem[i] / 65535.0f;
	}

	m_texture_handle = bgfx::createTexture2D(
		m_width, m_height, 1, bgfx::TextureFormat::R32F, 0, nullptr);
	bgfx::updateTexture2D(
		m_texture_handle,
		0,
		0,
		0,
		m_width,
		m_height,
		mem);
	return bgfx::isValid(m_texture_handle);
}


bool Texture::loadTGA(FS::IFile& file)
{
	PROFILE_FUNCTION();
	TGAHeader header;
	file.read(&header, sizeof(header));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;
	if (header.dataType != 2)
	{
		g_log_error.log("renderer") << "Unsupported texture format "
									<< m_path.c_str();
		return false;
	}

	if (color_mode < 3)
	{
		g_log_error.log("renderer") << "Unsupported color mode "
									<< m_path.c_str();
		return false;
	}

	m_width = header.width;
	m_height = header.height;
	TextureManager* manager = static_cast<TextureManager*>(
		getResourceManager().get(ResourceManager::TEXTURE));
	if (m_data_reference)
	{
		m_data.resize(image_size);
	}
	uint8_t* image_dest = m_data_reference
							  ? &m_data[0]
							  : (uint8_t*)manager->getBuffer(image_size);

	// Targa is BGR, swap to RGB, add alpha and flip Y axis
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0)
							   ? read_index
							   : y * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			file.read(&image_dest[write_index + 2], sizeof(uint8_t));
			file.read(&image_dest[write_index + 1], sizeof(uint8_t));
			file.read(&image_dest[write_index + 0], sizeof(uint8_t));
			if (color_mode == 4)
				file.read(&image_dest[write_index + 3], sizeof(uint8_t));
			else
				image_dest[write_index + 3] = 255;
			write_index += 4;
		}
	}
	m_BPP = 4;

	m_texture_handle = bgfx::createTexture2D(
		header.width,
		header.height,
		1,
		bgfx::TextureFormat::RGBA8,
		0,
		0);
	bgfx::updateTexture2D(
		m_texture_handle,
		0,
		0,
		0,
		header.width,
		header.height,
		bgfx::copy(image_dest, header.width * header.height * 4));
	return bgfx::isValid(m_texture_handle);
}


void Texture::addDataReference()
{
	++m_data_reference;
	if (m_data_reference == 1 && isReady())
	{
		m_resource_manager.get(ResourceManager::TEXTURE)->reload(*this);
	}
}


void Texture::removeDataReference()
{
	--m_data_reference;
	if (m_data_reference == 0)
	{
		m_data.clear();
	}
}


bool Texture::loadDDS(FS::IFile& file)
{
	bgfx::TextureInfo info;
	m_texture_handle = bgfx::createTexture(
		bgfx::copy(file.getBuffer(), file.size()), 0, 0, &info);
	m_BPP = -1;
	m_width = info.width;
	m_height = info.height;
	return bgfx::isValid(m_texture_handle);
}


void Texture::loaded(FS::IFile& file, bool success, FS::FileSystem& fs)
{
	PROFILE_FUNCTION();
	if (success)
	{
		const char* path = m_path.c_str();
		size_t len = m_path.length();
		bool loaded = false;
		if (len > 3 && strcmp(path + len - 4, ".dds") == 0)
		{
			loaded = loadDDS(file);
		}
		else if (len > 3 && strcmp(path + len - 4, ".raw") == 0)
		{
			loaded = loadRaw(file);
		}
		else
		{
			loaded = loadTGA(file);
		}
		if (!loaded)
		{
			g_log_warning.log("renderer") << "Error loading texture "
										  << m_path.c_str();
			onFailure();
		}
		else
		{
			m_size = file.size();
			decrementDepCount();
		}
	}
	else
	{
		g_log_warning.log("renderer") << "Error loading texture "
									  << m_path.c_str();
		onFailure();
	}
}


void Texture::doUnload(void)
{
	if (bgfx::isValid(m_texture_handle))
	{
		bgfx::destroyTexture(m_texture_handle);
		m_texture_handle = BGFX_INVALID_HANDLE;
	}
	m_data.clear();
	m_size = 0;
	onEmpty();
}


} // ~namespace Lumix
