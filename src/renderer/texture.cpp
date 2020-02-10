#include "engine/crt.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/path_utils.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"

namespace Lumix
{


const ResourceType Texture::TYPE("texture");


Texture::Texture(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& _allocator)
	: Resource(path, resource_manager, _allocator)
	, data_reference(0)
	, allocator(_allocator)
	, data(_allocator)
	, format(gpu::TextureFormat::RGBA8)
	, depth(-1)
	, layers(1)
	, width(0)
	, height(0)
	, mips(0)
	, renderer(renderer)
{
	flags = 0;
	is_cubemap = false;
	handle = gpu::INVALID_TEXTURE;
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
		logWarning("Renderer") << "Trying to set different flags for texture " << getPath().c_str()
									  << ". They are ignored.";
		return;
	}
	this->flags = flags;
}


void Texture::destroy()
{
	doUnload();
}


bool Texture::create(u32 w, u32 h, gpu::TextureFormat format, const void* data, u32 size)
{
	Renderer::MemRef memory = renderer.copy(data, size);
	handle = renderer.createTexture(w, h, 1, format, getGPUFlags() | (u32)gpu::TextureFlags::NO_MIPS, memory, getPath().c_str());
	mips = 1;
	width = w;
	height = h;

	const bool isReady = handle.isValid();
	onCreated(isReady ? State::READY : State::FAILURE);

	return isReady;
}


u32 Texture::getPixelNearest(u32 x, u32 y) const
{
	if (data.empty() || x >= width || y >= height || x < 0 || y < 0 || format != gpu::TextureFormat::RGBA8) return 0;

	return *(u32*)&data.getData()[(x + y * width) * 4];
}


u32 Texture::getPixel(float x, float y) const
{
	ASSERT(format == gpu::TextureFormat::RGBA8);
	if (data.empty() || x >= width || y >= height || x < 0 || y < 0)
	{
		return 0;
	}

	// http://fastcpp.blogspot.sk/2011/06/bilinear-pixel-interpolation-using-sse.html
	int px = (int)x;
	int py = (int)y;
	const u32* p0 = (u32*)&data.getData()[(px + py * width) * 4];

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


unsigned int Texture::compareTGA(IInputStream* file1, IInputStream* file2, int difference, IAllocator& allocator)
{
	TGAHeader header1, header2;
	file1->read(&header1, sizeof(header1));
	file2->read(&header2, sizeof(header2));

	if (header1.bitsPerPixel != header2.bitsPerPixel ||
		header1.width != header2.width || header1.height != header2.height ||
		header1.dataType != header2.dataType ||
		header1.imageDescriptor != header2.imageDescriptor)
	{
		logError("Renderer") << "Trying to compare textures with different formats";
		return 0xffffFFFF;
	}

	int color_mode = header1.bitsPerPixel / 8;
	if (header1.dataType != 2)
	{
		logError("Renderer") << "Unsupported texture format";
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
			if (abs(img1[i + j] - img2[i + j]) > difference)
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


bool Texture::saveTGA(IOutputStream* file,
	int width,
	int height,
	gpu::TextureFormat format,
	const u8* image_dest,
	bool upper_left_origin,
	const Path& path,
	IAllocator& allocator)
{
	if (format != gpu::TextureFormat::RGBA8)
	{
		logError("Renderer") << "Texture " << path.c_str() << " could not be saved, unsupported TGA format";
		return false;
	}

	u8* data = (u8*)allocator.allocate(width * height * 4);

	TGAHeader header;
	memset(&header, 0, sizeof(header));
	header.bitsPerPixel = (char)(4 * 8);
	header.height = (short)height;
	header.width = (short)width;
	header.dataType = 2;
	header.imageDescriptor = upper_left_origin ? 32 : 0;

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
		logError("Renderer") << "Texture " << texture.getPath().c_str()
									<< " could not be saved, no data was loaded";
		return;
	}

	OS::OutputFile file;
	FileSystem& fs = texture.getResourceManager().getOwner().getFileSystem();
	if (!fs.open(texture.getPath().c_str(), Ref(file))) {
		logError("Renderer") << "Failed to create file " << texture.getPath();
		return;
	}

	Texture::saveTGA(&file,
		texture.width,
		texture.height,
		texture.format,
		texture.data.getData(),
		false,
		texture.getPath(),
		texture.allocator);

	file.close();
}


void Texture::save()
{
	char ext[5];
	ext[0] = 0;
	PathUtils::getExtension(Span(ext), Span(getPath().c_str(), getPath().length()));
	if (equalStrings(ext, "raw") && format == gpu::TextureFormat::R16)
	{
		FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
		OS::OutputFile file;
		if (!fs.open(getPath().c_str(), Ref(file))) {
			logError("Renderer") << "Failed to create file " << getPath();
			return;
		}

		RawTextureHeader header;
		header.channels_count = 1;
		header.channel_type = RawTextureHeader::ChannelType::U16;
		header.is_array = false;
		header.width = width;
		header.height = height;
		header.depth = depth;

		file.write(&header, sizeof(header));
		file.write(data.getData(), data.getPos());
		file.close();
	}
	else if (equalStrings(ext, "tga") && format == gpu::TextureFormat::RGBA8)
	{
		Lumix::saveTGA(*this);
	}
	else
	{
		logError("Renderer") << "Texture " << getPath().c_str() << " can not be saved - unsupported format";
	}
}


void Texture::onDataUpdated(u32 x, u32 y, u32 w, u32 h)
{
	PROFILE_FUNCTION();

	u32 bytes_per_pixel = getBytesPerPixel(format);

	const u8* src_mem = (const u8*)data.getData();
	const Renderer::MemRef mem = renderer.allocate(w * h * bytes_per_pixel);
	u8* dst_mem = (u8*)mem.data;

	for (u32 j = 0; j < h; ++j) {
		memcpy(
			&dst_mem[(j * w) * bytes_per_pixel],
			&src_mem[(x + (y + j) * width) * bytes_per_pixel],
			bytes_per_pixel * w);
	}
	renderer.updateTexture(handle, x, y, w, h, format, mem);
}


static bool loadRaw(Texture& texture, InputMemoryStream& file, IAllocator& allocator)
{
	PROFILE_FUNCTION();
	RawTextureHeader header;
	file.read(&header, sizeof(header));
	if (header.magic != RawTextureHeader::MAGIC) {
		logError("Renderer") << texture.getPath() << ": corruptede file or not raw texture format.";
		return false;
	}
	if (header.version > RawTextureHeader::LAST_VERSION) {
		logError("Renderer") << texture.getPath() << ": unsupported version.";
		return false;
	}

	texture.width = header.width;
	texture.height = header.height;
	texture.depth = header.is_array ? 1 : header.depth;
	texture.layers = header.is_array ? header.depth : 1;
	switch(header.channel_type) {
		case RawTextureHeader::ChannelType::FLOAT:
			switch (header.channels_count) {
				case 1: texture.format = gpu::TextureFormat::R32F; break;
				case 4: texture.format = gpu::TextureFormat::RGBA32F; break;
				default: ASSERT(false); return false;
			}
			break;
		case RawTextureHeader::ChannelType::U8:
			switch (header.channels_count) {
				case 1: texture.format = gpu::TextureFormat::R8; break;
				case 4: texture.format = gpu::TextureFormat::RGBA8; break;
				default: ASSERT(false); return false;
			}
			break;
		case RawTextureHeader::ChannelType::U16:
			switch (header.channels_count) {
				case 1: texture.format = gpu::TextureFormat::R16; break;
				case 4: texture.format = gpu::TextureFormat::RGBA16; break;
				default: ASSERT(false); return false;
			}
			break;
		default: ASSERT(false); return false;
	}

	const u64 size = file.size() - file.getPosition();
	const u8* data = (const u8*)file.getBuffer() + file.getPosition();

	if (texture.data_reference) {
		texture.data.resize((int)size);
		file.read(texture.data.getMutableData(), size);
	}

	const Renderer::MemRef dst_mem = texture.renderer.copy(data, (u32)size);

	const u32 flag_3d = header.depth > 1 && !header.is_array ? (u32)gpu::TextureFlags::IS_3D : 0;

	texture.handle = texture.renderer.createTexture(texture.width
		, texture.height
		, texture.depth
		, texture.format
		, (texture.getGPUFlags() & ~(u32)gpu::TextureFlags::SRGB) | flag_3d | (u32)gpu::TextureFlags::NO_MIPS 
		, dst_mem
		, texture.getPath().c_str());
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


bool Texture::loadTGA(IInputStream& file)
{
	PROFILE_FUNCTION();
	TGAHeader header;
	file.read(&header, sizeof(header));

	int image_size = header.width * header.height * 4;
	if (header.dataType != 2 && header.dataType != 10)
	{
		int w, h, cmp;
		stbi_uc* stb_data = stbi_load_from_memory(static_cast<const stbi_uc*>(file.getBuffer()) + 7, (int)file.size() - 7, &w, &h, &cmp, 4);
		if (!stb_data) {
			logError("Renderer") << "Unsupported texture format " << getPath().c_str();
			return false;
		}
		Renderer::MemRef mem;
		if (!data_reference) mem = renderer.allocate(image_size);
		u8* image_dest = data_reference ? data.getMutableData() : (u8*)mem.data;
		memcpy(image_dest, stb_data, image_size);
		stbi_image_free(stb_data);

		//if ((header.imageDescriptor & 32) == 0) flipVertical((u32*)image_dest, header.width, header.height);

		is_cubemap = false;
		width = header.width;
		height = header.height;
		mips = 1;
		if (data_reference) mem = renderer.copy(image_dest, image_size);
		const bool is_srgb = flags & (u32)gpu::TextureFlags::SRGB;
		format = is_srgb ? gpu::TextureFormat::SRGBA : gpu::TextureFormat::RGBA8;
		handle = renderer.createTexture(header.width
			, header.height
			, 1
			, format
			, getGPUFlags() & ~(u32)gpu::TextureFlags::SRGB
			, mem
			, getPath().c_str());
		depth = 1;
		layers = 1;
		return handle.isValid();
	}

	if (header.bitsPerPixel < 24)
	{
		logError("Renderer") << "Unsupported color mode " << getPath().c_str();
		return false;
	}

	width = header.width;
	height = header.height;
	int pixel_count = width * height;
	is_cubemap = false;
	if (data_reference) data.resize(image_size);

	Renderer::MemRef mem;
	if (!data_reference) mem = renderer.allocate(image_size);

	u8* image_dest = data_reference ? data.getMutableData() : (u8*)mem.data;

	u32 bytes_per_pixel = header.bitsPerPixel / 8;
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
				u8* LUMIX_RESTRICT cursor = &image_dest[idx];
				const u8* row_end = cursor + header.width * bytes_per_pixel;
				while(cursor != row_end)
				{
					const u8 tmp = cursor[0];
					cursor[0] = cursor[2];
					cursor[2] = tmp;
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
					image_dest[idx + 3] = 255;
					idx += 4;
				}
			}
		}
	}
	if ((header.imageDescriptor & 32) == 0) flipVertical((u32*)image_dest, header.width, header.height);

	mips = 1;
	if (data_reference) mem = renderer.copy(image_dest, image_size);
	const bool is_srgb = flags & (u32)gpu::TextureFlags::SRGB;
	format = is_srgb ? gpu::TextureFormat::SRGBA : gpu::TextureFormat::RGBA8;
	handle = renderer.createTexture(header.width
		, header.height
		, 1
		, format
		, getGPUFlags() & ~(u32)gpu::TextureFlags::SRGB
		, mem
		, getPath().c_str());
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


static bool loadDDS(Texture& texture, IInputStream& file)
{
	if(texture.data_reference > 0) {
		logError("Renderer") << "Unsupported texture format " << texture.getPath() << " to access on CPU. Convert to TGA or RAW.";
		return false;
	}

	gpu::TextureInfo info;
	const u8* data = (const u8*)file.getBuffer();
	Renderer::MemRef mem = texture.renderer.copy(data + 7, (int)file.size() - 7);
	texture.handle = texture.renderer.loadTexture(mem, texture.getGPUFlags(), &info, texture.getPath().c_str());
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


u32 Texture::getGPUFlags() const
{
	u32 gpu_flags = 0;
	if(flags & (u32)Flags::SRGB) {
		gpu_flags  |= (u32)gpu::TextureFlags::SRGB;
	}
	if(flags & (u32)Flags::POINT) {
		gpu_flags  |= (u32)gpu::TextureFlags::POINT_FILTER;
	}
	if (flags & (u32)Flags::CLAMP_U) {
		gpu_flags |= (u32)gpu::TextureFlags::CLAMP_U;
	}
	if (flags & (u32)Flags::CLAMP_V) {
		gpu_flags |= (u32)gpu::TextureFlags::CLAMP_V;
	}
	if (flags & (u32)Flags::CLAMP_W) {
		gpu_flags |= (u32)gpu::TextureFlags::CLAMP_W;
	}
	return gpu_flags;
}


bool Texture::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();
	Profiler::pushString(getPath().c_str());
	char ext[4] = {};
	InputMemoryStream file(mem, size);
	if (!file.read(ext, 3)) return false;
	if (!file.read(&flags, sizeof(flags))) return false;

	bool loaded = false;
	if (equalIStrings(ext, "dds")) {
		loaded = loadDDS(*this, file);
	}
	else if (equalIStrings(ext, "raw")) {
		loaded = loadRaw(*this, file, allocator);
	}
	else {
		loaded = loadTGA(file);
	}
	if (!loaded) {
		logWarning("Renderer") << "Error loading texture " << getPath();
		return false;
	}

	m_size = file.size() - 3;
	return true;
}


void Texture::unload()
{
	if (handle.isValid()) {
		renderer.destroy(handle);
		handle = gpu::INVALID_TEXTURE;
	}
	data.clear();
}


} // namespace Lumix
