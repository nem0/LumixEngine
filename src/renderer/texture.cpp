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


Texture::Texture(const Path& path, ResourceManagerBase& resource_manager, IAllocator& _allocator)
	: Resource(path, resource_manager, _allocator)
	, data_reference(0)
	, allocator(_allocator)
	, data(_allocator)
	, bytes_per_pixel(-1)
	, depth(-1)
	, layers(1)
{
	bgfx_flags = 0;
	is_cubemap = false;
	handle = BGFX_INVALID_HANDLE;
}


Texture::~Texture()
{
	ASSERT(isEmpty());
}


void Texture::setFlag(u32 flag, bool value)
{
	u32 new_flags = bgfx_flags & ~flag;
	new_flags |= value ? flag : 0;
	bgfx_flags = new_flags;

	m_resource_manager.reload(*this);
}


void Texture::setFlags(u32 flags)
{
	if (isReady() && bgfx_flags != flags)
	{
		g_log_warning.log("Renderer") << "Trying to set different flags for texture " << getPath().c_str()
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
		handle = bgfx::createTexture2D(
			(uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, bgfx_flags, bgfx::copy(data, w * h * 4));
	}
	else
	{
		handle = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, bgfx_flags);
	}
	mips = 1;
	width = w;
	height = h;

	bool isReady = bgfx::isValid(handle);
	onCreated(isReady ? State::READY : State::FAILURE);

	return isReady;
}


u32 Texture::getPixelNearest(int x, int y) const
{
	if (data.empty() || x >= width || y >= height || x < 0 || y < 0 || bytes_per_pixel != 4) return 0;

	return *(u32*)&data[(x + y * width) * 4];
}


u32 Texture::getPixel(float x, float y) const
{
	ASSERT(bytes_per_pixel == 4);
	if (data.empty() || x >= width || y >= height || x < 0 || y < 0)
	{
		return 0;
	}

	// http://fastcpp.blogspot.sk/2011/06/bilinear-pixel-interpolation-using-sse.html
	int px = (int)x;
	int py = (int)y;
	const u32* p0 = (u32*)&data[(px + py * width) * 4];

	const u8* p1 = (u8*)p0;
	const u8* p2 = (u8*)(p0 + 1);
	const u8* p3 = (u8*)(p0 + width);
	const u8* p4 = (u8*)(p0 + 1 + width);

	float fx = x - px;
	float fy = y - py;
	float fx1 = 1.0f - fx;
	float fy1 = 1.0f - fy;

	int w1 = (int)(fx1 * fy1 * 256.0f);
	int w2 = (int)(fx * fy1 * 256.0f);
	int w3 = (int)(fx1 * fy * 256.0f);
	int w4 = (int)(fx * fy * 256.0f);

	u8 res[4];
	res[0] = (u8)((p1[0] * w1 + p2[0] * w2 + p3[0] * w3 + p4[0] * w4) >> 8);
	res[1] = (u8)((p1[1] * w1 + p2[1] * w2 + p3[1] * w3 + p4[1] * w4) >> 8);
	res[2] = (u8)((p1[2] * w1 + p2[2] * w2 + p3[2] * w3 + p4[2] * w4) >> 8);
	res[3] = (u8)((p1[3] * w1 + p2[3] * w2 + p3[3] * w3 + p4[3] * w4) >> 8);

	return *(u32*)res;
}


unsigned int Texture::compareTGA(FS::IFile* file1, FS::IFile* file2, int difference, IAllocator& allocator)
{
	TGAHeader header1, header2;
	file1->read(&header1, sizeof(header1));
	file2->read(&header2, sizeof(header2));

	if (header1.bitsPerPixel != header2.bitsPerPixel ||
		header1.width != header2.width || header1.height != header2.height ||
		header1.dataType != header2.dataType ||
		header1.imageDescriptor != header2.imageDescriptor)
	{
		g_log_error.log("Renderer") << "Trying to compare textures with different formats";
		return 0xffffFFFF;
	}

	int color_mode = header1.bitsPerPixel / 8;
	if (header1.dataType != 2)
	{
		g_log_error.log("Renderer") << "Unsupported texture format";
		return 0xffffFFFF;
	}

	int different_pixel_count = 0;
	size_t pixel_count = header1.width * header1.height;
	u8* img1 = (u8*)allocator.allocate(pixel_count * color_mode);
	u8* img2 = (u8*)allocator.allocate(pixel_count * color_mode);

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


bool Texture::saveTGA(FS::IFile* file,
	int width,
	int height,
	int bytes_per_pixel,
	const u8* image_dest,
	const Path& path,
	IAllocator& allocator)
{
	if (bytes_per_pixel != 4)
	{
		g_log_error.log("Renderer") << "Texture " << path.c_str() << " could not be saved, unsupported TGA format";
		return false;
	}

	u8* data = (u8*)allocator.allocate(width * height * 4);

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

	Texture::saveTGA(file,
		texture.width,
		texture.height,
		texture.bytes_per_pixel,
		&texture.data[0],
		texture.getPath(),
		texture.allocator);

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
	PROFILE_FUNCTION();

	const bgfx::Memory* mem = nullptr;

	if (bytes_per_pixel == 2)
	{
		const u16* src_mem = (const u16*)&data[0];
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
		ASSERT(bytes_per_pixel == 4);
		const u8* src_mem = (const u8*)&data[0];
		mem = bgfx::alloc(w * h * bytes_per_pixel);
		u8* dst_mem = mem->data;

		for (int j = 0; j < h; ++j)
		{
			copyMemory(
				&dst_mem[(j * w) * bytes_per_pixel],
				&src_mem[(x + (y + j) * width) * bytes_per_pixel],
				bytes_per_pixel * w);
		}
	}
	bgfx::updateTexture2D(handle, 0, 0, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, mem);
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

	const u16* src_mem = (const u16*)file.getBuffer();
	const bgfx::Memory* mem = bgfx::alloc(texture.width * texture.height * sizeof(float));
	float* dst_mem = (float*)mem->data;

	for (int i = 0; i < texture.width * texture.height; ++i)
	{
		dst_mem[i] = src_mem[i] / 65535.0f;
	}

	texture.handle = bgfx::createTexture2D((uint16_t)texture.width,
		(uint16_t)texture.height,
		false,
		1,
		bgfx::TextureFormat::R32F,
		texture.bgfx_flags,
		nullptr);
	// update must be here because texture is immutable otherwise 
	bgfx::updateTexture2D(texture.handle, 0, 0, 0, 0, (uint16_t)texture.width, (uint16_t)texture.height, mem);
	texture.depth = 1;
	texture.layers = 1;
	texture.mips = 1;
	texture.is_cubemap = false;
	return bgfx::isValid(texture.handle);
}


bool Texture::loadTGA(FS::IFile& file, TGAHeader& header, Array<u8>& data, const char* path)
{
	PROFILE_FUNCTION();
	file.read(&header, sizeof(header));

	int bytes_per_pixel = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;
	if (header.dataType != 2 && header.dataType != 10)
	{
		g_log_error.log("Renderer") << "Unsupported texture format " << path;
		return false;
	}

	if (bytes_per_pixel < 3)
	{
		g_log_error.log("Renderer") << "Unsupported color mode " << path;
		return false;
	}

	int pixel_count = header.width * header.height;
	data.resize(image_size);
	u8* image_dest = &data[0];

	bool is_rle = header.dataType == 10;
	if (is_rle)
	{
		u8* out = image_dest;
		u8 byte;
		struct Pixel {
			u8 uint8[4];
		} pixel;
		do
		{
			file.read(&byte, sizeof(byte));
			if (byte < 128)
			{
				u8 count = byte + 1;
				for (u8 i = 0; i < count; ++i)
				{
					file.read(&pixel, bytes_per_pixel);
					out[0] = pixel.uint8[2];
					out[1] = pixel.uint8[1];
					out[2] = pixel.uint8[0];
					if (bytes_per_pixel == 4) out[3] = pixel.uint8[3];
					else out[3] = 255;
					out += 4;
				}
			}
			else
			{
				byte -= 127;
				file.read(&pixel, bytes_per_pixel);
				for (int i = 0; i < byte; ++i)
				{
					out[0] = pixel.uint8[2];
					out[1] = pixel.uint8[1];
					out[2] = pixel.uint8[0];
					if (bytes_per_pixel == 4) out[3] = pixel.uint8[3];
					else out[3] = 255;
					out += 4;
				}
			}
		} while (out - image_dest < pixel_count * 4);
	}
	else
	{
		for (long y = 0; y < header.height; y++)
		{
			long idx = y * header.width * 4;
			for (long x = 0; x < header.width; x++)
			{
				file.read(&image_dest[idx + 2], sizeof(u8));
				file.read(&image_dest[idx + 1], sizeof(u8));
				file.read(&image_dest[idx + 0], sizeof(u8));
				if (bytes_per_pixel == 4)
					file.read(&image_dest[idx + 3], sizeof(u8));
				else
					image_dest[idx + 3] = 255;
				idx += 4;
			}
		}
	}
	return true;
}


bool Texture::loadTGA(FS::IFile& file)
{
	PROFILE_FUNCTION();
	TGAHeader header;
	file.read(&header, sizeof(header));

	bytes_per_pixel = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;
	if (header.dataType != 2 && header.dataType != 10)
	{
		g_log_error.log("Renderer") << "Unsupported texture format " << getPath().c_str();
		return false;
	}

	if (bytes_per_pixel < 3)
	{
		g_log_error.log("Renderer") << "Unsupported color mode " << getPath().c_str();
		return false;
	}

	width = header.width;
	height = header.height;
	int pixel_count = width * height;
	is_cubemap = false;
	TextureManager& manager = static_cast<TextureManager&>(getResourceManager());
	if (data_reference)
	{
		data.resize(image_size);
	}
	u8* image_dest = data_reference ? &data[0] : (u8*)manager.getBuffer(image_size);

	bool is_rle = header.dataType == 10;
	if (is_rle)
	{
		u8* out = image_dest;
		u8 byte;
		struct Pixel {
			u8 uint8[4];
		} pixel;
		do
		{
			file.read(&byte, sizeof(byte));
			if (byte < 128)
			{
				u8 count = byte + 1;
				for (u8 i = 0; i < count; ++i)
				{
					file.read(&pixel, bytes_per_pixel);
					out[0] = pixel.uint8[2];
					out[1] = pixel.uint8[1];
					out[2] = pixel.uint8[0];
					if (bytes_per_pixel == 4) out[3] = pixel.uint8[3];
					else out[3] = 255;
					out += 4;
				}
			}
			else
			{
				byte -= 127;
				file.read(&pixel, bytes_per_pixel);
				for (int i = 0; i < byte; ++i)
				{
					out[0] = pixel.uint8[2];
					out[1] = pixel.uint8[1];
					out[2] = pixel.uint8[0];
					if (bytes_per_pixel == 4) out[3] = pixel.uint8[3];
					else out[3] = 255;
					out += 4;
				}
			}
		} while (out - image_dest < pixel_count * 4);
	}
	else
	{
		for (long y = 0; y < header.height; y++)
		{
			long idx = y * header.width * 4;
			for (long x = 0; x < header.width; x++)
			{
				file.read(&image_dest[idx + 2], sizeof(u8));
				file.read(&image_dest[idx + 1], sizeof(u8));
				file.read(&image_dest[idx + 0], sizeof(u8));
				if (bytes_per_pixel == 4)
					file.read(&image_dest[idx + 3], sizeof(u8));
				else
					image_dest[idx + 3] = 255;
				idx += 4;
			}
		}
	}
	bytes_per_pixel = 4;
	mips = 1;
	handle = bgfx::createTexture2D(
		header.width,
		header.height,
		false,
		0,
		bgfx::TextureFormat::RGBA8,
		bgfx_flags,
		nullptr);
	// update must be here because texture is immutable otherwise 
	bgfx::updateTexture2D(handle,
		0,
		0,
		0,
		0,
		header.width,
		header.height,
		bgfx::copy(image_dest, header.width * header.height * 4));
	depth = 1;
	layers = 1;
	return bgfx::isValid(handle);
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


static bool loadDDSorKTX(Texture& texture, FS::IFile& file)
{
	bgfx::TextureInfo info;
	const auto* mem = bgfx::copy(file.getBuffer(), (u32)file.size());
	texture.handle = bgfx::createTexture(mem, texture.bgfx_flags, 0, &info);
	texture.width = info.width;
	texture.mips = info.numMips;
	texture.height = info.height;
	texture.depth = info.depth;
	texture.layers = info.numLayers;
	texture.is_cubemap = info.cubeMap;
	return bgfx::isValid(texture.handle);
}


bool Texture::load(FS::IFile& file)
{
	PROFILE_FUNCTION();

	const char* path = getPath().c_str();
	size_t len = getPath().length();
	bool loaded = false;
	if (len > 3 && (equalStrings(path + len - 4, ".dds") || equalStrings(path + len - 4, ".ktx")))
	{
		loaded = loadDDSorKTX(*this, file);
	}
	else if (len > 3 && equalStrings(path + len - 4, ".raw"))
	{
		loaded = loadRaw(*this, file);
	}
	else
	{
		loaded = loadTGA(file);
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


} // namespace Lumix
