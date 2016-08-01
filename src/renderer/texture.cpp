#include "engine/fs/file_system.h"
#include "engine/log.h"
#include "engine/math_utils.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include <bgfx/bgfx.h>
#include <cmath>

namespace Lumix
{


static const ResourceType TEXTURE_TYPE("texture");


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


Texture::Texture(const Path& path, ResourceManagerBase& resource_manager, IAllocator& _allocator)
	: Resource(path, resource_manager, _allocator)
	, data_reference(0)
	, allocator(_allocator)
	, data(_allocator)
	, bytes_per_pixel(-1)
	, depth(-1)
{
	atlas_size = -1;
	bgfx_flags = 0;
	is_cubemap = false;
	handle = BGFX_INVALID_HANDLE;
}


Texture::~Texture()
{
	ASSERT(isEmpty());
}


void Texture::setFlag(uint32 flag, bool value)
{
	uint32 new_flags = bgfx_flags & ~flag;
	new_flags |= value ? flag : 0;
	bgfx_flags = new_flags;

	m_resource_manager.reload(*this);
}


void Texture::setFlags(uint32 flags)
{
	if (isReady() && bgfx_flags != flags)
	{
		g_log_warning.log("Renderer")
			<< "Trying to set different flags for texture " << getPath().c_str()
			<< ". They are ignored.";
		return;
	}
	bgfx_flags = flags;
}


void Texture::destroy()
{
	doUnload();
}


bool Texture::create(int w, int h, void* data)
{
	if (data)
	{
		handle = bgfx::createTexture2D((uint16_t)w,
			(uint16_t)h,
			1,
			bgfx::TextureFormat::RGBA8,
			bgfx_flags,
			bgfx::copy(data, w * h * 4));
	}
	else
	{
		handle =
			bgfx::createTexture2D((uint16_t)w, (uint16_t)h, 1, bgfx::TextureFormat::RGBA8, bgfx_flags);
	}


	bool isReady = bgfx::isValid(handle);
	onCreated(isReady ? State::READY : State::FAILURE);

	return isReady;
}


uint32 Texture::getPixelNearest(int x, int y) const
{
	if (data.empty() || x >= width || y >= height || x < 0 || y < 0 || bytes_per_pixel != 4) return 0;

	return *(uint32*)&data[(x + y * width) * 4];
}


uint32 Texture::getPixel(float x, float y) const
{
	if (data.empty() || x >= width || y >= height || x < 0 || y < 0)
	{
		return 0;
	}

	// http://fastcpp.blogspot.sk/2011/06/bilinear-pixel-interpolation-using-sse.html
	int px = (int)x;
	int py = (int)y;
	const uint32* p0 = (uint32*)&data[(px + py * width) * 4];

	const uint8* p1 = (uint8*)p0;
	const uint8* p2 = (uint8*)(p0 + 1);
	const uint8* p3 = (uint8*)(p0 + width);
	const uint8* p4 = (uint8*)(p0 + 1 + width);

	float fx = x - px;
	float fy = y - py;
	float fx1 = 1.0f - fx;
	float fy1 = 1.0f - fy;

	int w1 = (int)(fx1 * fy1 * 256.0f);
	int w2 = (int)(fx * fy1 * 256.0f);
	int w3 = (int)(fx1 * fy * 256.0f);
	int w4 = (int)(fx * fy * 256.0f);

	uint8 res[4];
	res[0] = (uint8)((p1[0] * w1 + p2[0] * w2 + p3[0] * w3 + p4[0] * w4) >> 8);
	res[1] = (uint8)((p1[1] * w1 + p2[1] * w2 + p3[1] * w3 + p4[1] * w4) >> 8);
	res[2] = (uint8)((p1[2] * w1 + p2[2] * w2 + p3[2] * w3 + p4[2] * w4) >> 8);
	res[3] = (uint8)((p1[3] * w1 + p2[3] * w2 + p3[3] * w3 + p4[3] * w4) >> 8);

	return *(uint32*)res;
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
		g_log_error.log("Renderer")
			<< "Trying to compare textures with different formats";
		return 0;
	}

	int color_mode = header1.bitsPerPixel / 8;
	if (header1.dataType != 2)
	{
		g_log_error.log("Renderer") << "Unsupported texture format";
		return 0;
	}

	int different_pixel_count = 0;
	size_t pixel_count = header1.width * header1.height;
	uint8* img1 = (uint8*)allocator.allocate(pixel_count * color_mode);
	uint8* img2 = (uint8*)allocator.allocate(pixel_count * color_mode);

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


static bool saveTGA(IAllocator& allocator,
	FS::IFile* file,
	int width,
	int height,
	int bytes_per_pixel,
	const uint8* image_dest,
	const Path& path)
{
	if (bytes_per_pixel != 4)
	{
		g_log_error.log("Renderer") << "Texture " << path.c_str() << " could not be saved, unsupported TGA format";
		return false;
	}

	uint8* data = (uint8*)allocator.allocate(width * height * 4);

	TGAHeader header;
	setMemory(&header, 0, sizeof(header));
	header.bitsPerPixel = (char)(bytes_per_pixel * 8);
	header.height = (short)height;
	header.width = (short)width;
	header.dataType = 2;

	file->write(&header, sizeof(header));

	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * 4;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : y * header.width * 4;
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


static void saveTGA(Texture& texture)
{
	if (texture.data.empty())
	{
		g_log_error.log("Renderer") << "Texture " << texture.getPath().c_str()
									<< " could not be saved, no data was loaded";
		return;
	}

	FS::FileSystem& fs = texture.getResourceManager().getOwner().getFileSystem();
	FS::IFile* file = fs.open(fs.getDefaultDevice(), texture.getPath(), FS::Mode::CREATE_AND_WRITE);

	saveTGA(texture.allocator,
		file,
		texture.width,
		texture.height,
		texture.bytes_per_pixel,
		&texture.data[0],
		texture.getPath());

	fs.close(*file);
}


void Texture::save()
{
	char ext[5];
	ext[0] = 0;
	PathUtils::getExtension(ext, 5, getPath().c_str());
	if (equalStrings(ext, "raw") && bytes_per_pixel == 2)
	{
		FS::FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(), getPath(), FS::Mode::CREATE_AND_WRITE);

		file->write(&data[0], data.size() * sizeof(data[0]));
		fs.close(*file);
	}
	else if (equalStrings(ext, "tga") && bytes_per_pixel == 4)
	{
		Lumix::saveTGA(*this);
	}
	else
	{
		g_log_error.log("Renderer") << "Texture " << getPath().c_str() << " can not be saved - unsupported format";
	}
}


void Texture::onDataUpdated(int x, int y, int w, int h)
{
	const bgfx::Memory* mem = nullptr;

	if (bytes_per_pixel == 2)
	{
		const uint16* src_mem = (const uint16*)&data[0];
		mem = bgfx::alloc(w * h * sizeof(float));
		float* dst_mem = (float*)mem->data;

		for (int j = 0; j < h; ++j)
		{
			for (int i = 0; i < w; ++i)
			{
				dst_mem[i + j * w] = src_mem[x + i + (y + j) * width] / 65535.0f;
			}
		}
	}
	else
	{
		const uint8* src_mem = (const uint8*)&data[0];
		mem = bgfx::alloc(w * h * bytes_per_pixel);
		uint8* dst_mem = mem->data;

		for (int j = 0; j < h; ++j)
		{
			for (int i = 0; i < w; ++i)
			{
				copyMemory(&dst_mem[(i + j * w) * bytes_per_pixel],
					&src_mem[(x + i + (y + j) * width) * bytes_per_pixel],
					bytes_per_pixel);
			}
		}
	}
	bgfx::updateTexture2D(handle, 0, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, mem);
}


bool loadRaw(Texture& texture, FS::IFile& file)
{
	PROFILE_FUNCTION();
	size_t size = file.size();
	texture.bytes_per_pixel = 2;
	texture.width = (int)sqrt(size / texture.bytes_per_pixel);
	texture.height = texture.width;

	if (texture.data_reference)
	{
		texture.data.resize((int)size);
		file.read(&texture.data[0], size);
	}

	const uint16* src_mem = (const uint16*)file.getBuffer();
	const bgfx::Memory* mem = bgfx::alloc(texture.width * texture.height * sizeof(float));
	float* dst_mem = (float*)mem->data;

	for (int i = 0; i < texture.width * texture.height; ++i)
	{
		dst_mem[i] = src_mem[i] / 65535.0f;
	}

	texture.handle = bgfx::createTexture2D(
		(uint16_t)texture.width, (uint16_t)texture.height, 1, bgfx::TextureFormat::R32F, texture.bgfx_flags, nullptr);
	bgfx::updateTexture2D(texture.handle, 0, 0, 0, (uint16_t)texture.width, (uint16_t)texture.height, mem);
	texture.depth = 1;
	texture.is_cubemap = false;
	return bgfx::isValid(texture.handle);
}


static bool loadTGA(Texture& texture, FS::IFile& file)
{
	PROFILE_FUNCTION();
	TGAHeader header;
	file.read(&header, sizeof(header));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;
	if (header.dataType != 2)
	{
		g_log_error.log("Renderer") << "Unsupported texture format " << texture.getPath().c_str();
		return false;
	}

	if (color_mode < 3)
	{
		g_log_error.log("Renderer") << "Unsupported color mode " << texture.getPath().c_str();
		return false;
	}

	texture.width = header.width;
	texture.height = header.height;
	texture.is_cubemap = false;
	TextureManager& manager = static_cast<TextureManager&>(texture.getResourceManager());
	if (texture.data_reference)
	{
		texture.data.resize(image_size);
	}
	uint8* image_dest = texture.data_reference ? &texture.data[0] : (uint8*)manager.getBuffer(image_size);

	// Targa is BGR, swap to RGB, add alpha and flip Y axis
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0)
							   ? read_index
							   : y * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			file.read(&image_dest[write_index + 2], sizeof(uint8));
			file.read(&image_dest[write_index + 1], sizeof(uint8));
			file.read(&image_dest[write_index + 0], sizeof(uint8));
			if (color_mode == 4)
				file.read(&image_dest[write_index + 3], sizeof(uint8));
			else
				image_dest[write_index + 3] = 255;
			write_index += 4;
		}
	}
	texture.bytes_per_pixel = 4;

	texture.handle = bgfx::createTexture2D(
		header.width,
		header.height,
		1,
		bgfx::TextureFormat::RGBA8,
		texture.bgfx_flags,
		0);
	bgfx::updateTexture2D(
		texture.handle,
		0,
		0,
		0,
		header.width,
		header.height,
		bgfx::copy(image_dest, header.width * header.height * 4));
	texture.depth = 1;
	return bgfx::isValid(texture.handle);
}


void Texture::addDataReference()
{
	++data_reference;
	if (data_reference == 1 && isReady())
	{
		m_resource_manager.reload(*this);
	}
}


void Texture::removeDataReference()
{
	--data_reference;
	if (data_reference == 0)
	{
		data.clear();
	}
}


static bool loadDDS(Texture& texture, FS::IFile& file)
{
	bgfx::TextureInfo info;
	const auto* mem = bgfx::copy(file.getBuffer(), (uint32)file.size());
	texture.handle = bgfx::createTexture(mem, texture.bgfx_flags, 0, &info);
	texture.bytes_per_pixel = -1;
	texture.width = info.width;
	texture.height = info.height;
	texture.depth = info.depth;
	texture.is_cubemap = info.cubeMap;
	return bgfx::isValid(texture.handle);
}


bool Texture::load(FS::IFile& file)
{
	PROFILE_FUNCTION();

	const char* path = getPath().c_str();
	size_t len = getPath().length();
	bool loaded = false;
	if (len > 3 && equalStrings(path + len - 4, ".dds"))
	{
		loaded = loadDDS(*this, file);
	}
	else if (len > 3 && equalStrings(path + len - 4, ".raw"))
	{
		loaded = loadRaw(*this, file);
	}
	else
	{
		loaded = loadTGA(*this, file);
	}
	if (!loaded)
	{
		g_log_warning.log("Renderer") << "Error loading texture " << path;
		return false;
	}

	m_size = file.size();
	return true;
}


void Texture::unload(void)
{
	if (bgfx::isValid(handle))
	{
		bgfx::destroyTexture(handle);
		handle = BGFX_INVALID_HANDLE;
	}
	data.clear();
}


} // ~namespace Lumix
