#include "engine/fs/file_system.h"
#include "engine/log.h"
#include "engine/math_utils.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include <cmath>

namespace Lumix
{


const ResourceType Texture::TYPE("texture");


Texture::Texture(const Path& path, Renderer& renderer, ResourceManager& resource_manager, IAllocator& _allocator)
	: Resource(path, resource_manager, _allocator)
	, data_reference(0)
	, allocator(_allocator)
	, data(_allocator)
	, bytes_per_pixel(-1)
	, depth(-1)
	, layers(1)
	, renderer(renderer)
{
	flags = 0;
	is_cubemap = false;
	handle = ffr::INVALID_TEXTURE;
}


Texture::~Texture()
{
	ASSERT(isEmpty());
}


bool Texture::getFlag(Flags flag)
{
	return flags & u32(flag);
}


void Texture::setFlag(Flags flag, bool value)
{
	u32 new_flags = flags & ~u32(flag);
	new_flags |= value ? u32(flag) : 0;
	flags = new_flags;
}


void Texture::setFlags(u32 flags)
{
	if (isReady() && this->flags != flags)
	{
		g_log_warning.log("Renderer") << "Trying to set different flags for texture " << getPath().c_str()
									  << ". They are ignored.";
		return;
	}
	this->flags = flags;
}


void Texture::destroy()
{
	doUnload();
}


bool Texture::create(int w, int h, const void* data, uint size)
{
	Renderer::MemRef memory = renderer.copy(data, size);
	handle = renderer.createTexture(w, h, 1, ffr::TextureFormat::RGBA8, getFFRFlags(), memory, getPath().c_str());
	mips = 1;
	width = w;
	height = h;

	const bool isReady = handle.isValid();
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
	header.imageDescriptor = 32;

	file->write(&header, sizeof(header));

	for (long y = 0; y < header.height; y++)
	{
		long write_index = y * header.width * 4;
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
	ASSERT(false);
	// TODO
	/*PROFILE_FUNCTION();

	const bgfx::Memory* mem;

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
	bgfx::updateTexture2D(handle, 0, 0, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, mem);*/
}


bool loadRaw(Texture& texture, FS::IFile& file, IAllocator& allocator)
{
	PROFILE_FUNCTION();
	size_t size = file.size() - 3;
	texture.bytes_per_pixel = 2;
	texture.width = (int)sqrt(size / texture.bytes_per_pixel);
	texture.height = texture.width;

	if (texture.data_reference)
	{
		texture.data.resize((int)size);
		file.read(&texture.data[0], size);
	}

	const u8* data = (const u8*)file.getBuffer();

	const u16* src_mem = (const u16*)(data + 3);
	Array<float> dst_mem(allocator);
	dst_mem.resize(texture.width * texture.height * sizeof(float));

	for (int i = 0; i < texture.width * texture.height; ++i)
	{
		dst_mem[i] = src_mem[i] / 65535.0f;
	}

	const Renderer::MemRef mem = texture.renderer.copy(dst_mem.begin(), dst_mem.byte_size());
	texture.handle = texture.renderer.createTexture(texture.width, texture.height, 1, ffr::TextureFormat::R32F, texture.getFFRFlags(), mem, texture.getPath().c_str());
	texture.depth = 1;
	texture.layers = 1;
	texture.mips = 1;
	texture.is_cubemap = false;
	return texture.handle.isValid();
}


static void flipVertical(u32* image, int width, int height)
{
	PROFILE_FUNCTION();
	for (int j = 0; j < height / 2; ++j)
	{
		int row_offset = width * j;
		int inv_j = height - j - 1;
		int inv_row_offset = width * inv_j;
		for (int i = 0; i < width; ++i)
		{
			u32 tmp = image[i + row_offset];
			image[i + row_offset] = image[i + inv_row_offset];
			image[i + inv_row_offset] = tmp;
		}
	}
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
	if ((header.imageDescriptor & 32) == 0) flipVertical((u32*)image_dest, header.width, header.height);
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
		PROFILE_BLOCK("read rle");
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
		PROFILE_BLOCK("read");
		if (bytes_per_pixel == 4)
		{
			PROFILE_BLOCK("read 4BPP");
			file.read(image_dest, header.width * header.height * bytes_per_pixel);
			for (long y = 0; y < header.height; y++)
			{
				long idx = y * header.width * bytes_per_pixel;
				u8* cursor = &image_dest[idx];
				const u8* row_end = cursor + header.width * bytes_per_pixel;
				while(cursor != row_end)
				{
					Math::swap(cursor[0], cursor[2]);
					cursor += 4;
				}
			}
		}
		else
		{
			PROFILE_BLOCK("read 3BPP");
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
	}
	if ((header.imageDescriptor & 32) == 0) flipVertical((u32*)image_dest, header.width, header.height);

	bytes_per_pixel = 4;
	mips = 1;
	Renderer::MemRef mem = renderer.copy(image_dest, image_size);
	handle = renderer.createTexture(header.width, header.height, 1, ffr::TextureFormat::RGBA8, getFFRFlags(), mem, getPath().c_str());
	depth = 1;
	layers = 1;
	return handle.isValid();
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
	ffr::TextureInfo info;
	const u8* data = (const u8*)file.getBuffer();
	Renderer::MemRef mem = texture.renderer.copy(data + 7, (int)file.size() - 7);
	texture.handle = texture.renderer.loadTexture(mem, texture.getFFRFlags(), &info, texture.getPath().c_str());
	if (texture.handle.isValid()) {
		texture.width = info.width;
		texture.height = info.height;
		texture.mips = info.mips;
		texture.depth = info.depth;
		texture.layers = info.layers;
		texture.is_cubemap = info.is_cubemap;
	}

	return texture.handle.isValid();
}


u32 Texture::getFFRFlags() const
{
	u32 ffr_flags = 0;
	if(flags & (u32)Flags::SRGB) {
		ffr_flags  |= (u32)ffr::TextureFlags::SRGB;
	}
	if (flags & (u32)Flags::CLAMP) {
		ffr_flags |= (u32)ffr::TextureFlags::CLAMP;
	}

	return ffr_flags ;
}


bool Texture::load(FS::IFile& file)
{
	PROFILE_FUNCTION();
	char ext[4] = {};
	if (!file.read(ext, 3)) return false;
	u32 flags;
	if (!file.read(&flags, sizeof(flags))) return false;
	setSRGB(flags & (u32)Flags::SRGB);
	setFlag(Flags::CLAMP, flags & (u32)Flags::CLAMP);

	bool loaded = false;
	if (equalStrings(ext, "dds")) {
		loaded = loadDDS(*this, file);
	}
	else if (equalStrings(ext, "raw")) {
		loaded = loadRaw(*this, file, allocator);
	}
	else {
		loaded = loadTGA(file);
	}
	if (!loaded) {
		g_log_warning.log("Renderer") << "Error loading texture " << getPath();
		return false;
	}

	m_size = file.size() - 3;
	return true;
}


void Texture::unload()
{
	if (handle.isValid()) {
		renderer.destroy(handle);
		handle = ffr::INVALID_TEXTURE;
	}
	data.clear();
}


} // namespace Lumix
