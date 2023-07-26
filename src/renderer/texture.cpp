#ifdef LUMIX_BASIS_UNIVERSAL
	#define LUMIX_NO_CUSTOM_CRT
	#include <transcoder/basisu_transcoder.h>
#endif
#include "engine/crt.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/path.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "renderer/draw_stream.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"

namespace Lumix
{


const ResourceType Texture::TYPE("texture");


Texture::Texture(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& _allocator)
	: Resource(path, resource_manager, _allocator)
	, data_reference(0)
	, allocator(_allocator, m_path.c_str())
	, data(_allocator)
	, format(gpu::TextureFormat::RGBA8)
	, depth(1)
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
	if (isReady() && this->flags != flags) {
		logWarning("Trying to set different flags for texture ", getPath().c_str(), ". They are ignored.");
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
	handle = renderer.createTexture(w, h, 1, format, getGPUFlags() | gpu::TextureFlags::NO_MIPS, memory, getPath().c_str());
	mips = 1;
	width = w;
	height = h;

	const bool isReady = handle;
	onCreated(isReady ? State::READY : State::FAILURE);

	return isReady;
}


u32 Texture::getPixelNearest(u32 x, u32 y) const
{
	if (data.empty() || x >= width || y >= height || format != gpu::TextureFormat::RGBA8) return 0;

	return *(u32*)&data.data()[(x + y * width) * 4];
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
	const u32* p0 = (u32*)&data.data()[(px + py * width) * 4];

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

	alignas(u32) u8 res[4];
	res[0] = (u8)((p1[0] * w1 + p2[0] * w2 + p3[0] * w3 + p4[0] * w4) >> 8);
	res[1] = (u8)((p1[1] * w1 + p2[1] * w2 + p3[1] * w3 + p4[1] * w4) >> 8);
	res[2] = (u8)((p1[2] * w1 + p2[2] * w2 + p3[2] * w3 + p4[2] * w4) >> 8);
	res[3] = (u8)((p1[3] * w1 + p2[3] * w2 + p3[3] * w3 + p4[3] * w4) >> 8);

	return *(u32*)res;
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
	if (format != gpu::TextureFormat::RGBA8) {
		logError("Texture ", path, " could not be saved, unsupported TGA format");
		return false;
	}


	TGAHeader header;
	memset(&header, 0, sizeof(header));
	header.bitsPerPixel = (char)(4 * 8);
	header.height = (short)height;
	header.width = (short)width;
	header.dataType = 2;
	header.imageDescriptor = upper_left_origin ? 32 : 0;

	if (!file->write(&header, sizeof(header))) return false;

	u8* data = (u8*)allocator.allocate(width * height * 4);
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

	bool res = file->write(data, width * height * 4);
	allocator.deallocate(data);
	return res;
}


static void saveTGA(Texture& texture)
{
	if (texture.data.empty())
	{
		logError("Texture ", texture.getPath(), " could not be saved, no data was loaded");
		return;
	}

	os::OutputFile file;
	FileSystem& fs = texture.getResourceManager().getOwner().getFileSystem();
	if (!fs.open(texture.getPath().c_str(), file)) {
		logError("Failed to create file ", texture.getPath());
		return;
	}

	Texture::saveTGA(&file,
		texture.width,
		texture.height,
		texture.format,
		texture.data.data(),
		true,
		texture.getPath(),
		texture.allocator);

	file.close();
}


void Texture::save()
{
	char ext[5];
	makeLowercase(Span(ext), Path::getExtension(Span(getPath().c_str(), getPath().length())).begin());
	if (equalStrings(ext, "raw") && format == gpu::TextureFormat::R16)
	{
		FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
		os::OutputFile file;
		if (!fs.open(getPath().c_str(), file)) {
			logError("Failed to create file ", getPath());
			return;
		}

		RawTextureHeader header;
		header.channels_count = 1;
		header.channel_type = RawTextureHeader::ChannelType::U16;
		header.is_array = false;
		header.width = width;
		header.height = height;
		header.depth = depth;

		bool success = file.write(&header, sizeof(header));
		success = file.write(data.data(), data.size()) && success;
		if (!success) {
			logError("Failed to write ", getPath());
		}
		file.close();
	}
	else if (equalStrings(ext, "tga") && format == gpu::TextureFormat::RGBA8)
	{
		Lumix::saveTGA(*this);
	}
	else
	{
		logError("Texture ", getPath(), " can not be saved - unsupported format");
	}
}


void Texture::onDataUpdated(u32 x, u32 y, u32 w, u32 h)
{
	PROFILE_FUNCTION();

	u32 bytes_per_pixel = getBytesPerPixel(format);

	const u8* src_mem = (const u8*)data.data();
	const Renderer::MemRef mem = renderer.allocate(w * h * bytes_per_pixel);
	u8* dst_mem = (u8*)mem.data;

	for (u32 j = 0; j < h; ++j) {
		memcpy(
			&dst_mem[(j * w) * bytes_per_pixel],
			&src_mem[(x + (y + j) * width) * bytes_per_pixel],
			bytes_per_pixel * w);
	}

	DrawStream& stream = renderer.getDrawStream();
	stream.update(handle, 0, x, y, 0, w, h, format, mem.data, mem.size);
	stream.freeMemory(mem.data, renderer.getAllocator());
}


static bool loadRaw(Texture& texture, InputMemoryStream& file, IAllocator& allocator)
{
	PROFILE_FUNCTION();
	RawTextureHeader header;
	file.read(&header, sizeof(header));
	if (header.magic != RawTextureHeader::MAGIC) {
		logError(texture.getPath(), ": corrupted file or not raw texture format.");
		return false;
	}
	if (header.version > RawTextureHeader::LAST_VERSION) {
		logError(texture.getPath(), ": unsupported version.");
		return false;
	}

	texture.width = header.width;
	texture.height = header.height;
	texture.depth = header.depth;
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
	}

	const u64 size = file.size() - file.getPosition();
	const u8* data = (const u8*)file.getBuffer() + file.getPosition();

	if (texture.data_reference) {
		texture.data.resize((int)size);
		file.read(texture.data.getMutableData(), size);
	}

	const Renderer::MemRef dst_mem = texture.renderer.copy(data, (u32)size);

	const gpu::TextureFlags flag_3d = header.depth > 1 && !header.is_array ? gpu::TextureFlags::IS_3D : gpu::TextureFlags::NONE;

	texture.handle = texture.renderer.createTexture(texture.width
		, texture.height
		, texture.depth
		, texture.format
		, (texture.getGPUFlags() & ~gpu::TextureFlags::SRGB) | flag_3d | gpu::TextureFlags::NO_MIPS 
		, dst_mem
		, texture.getPath().c_str());
	texture.mips = 1;
	texture.is_cubemap = false;
	return texture.handle;
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
			logError("Unsupported texture format ", getPath());
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
		const bool is_srgb = flags & (u32)Flags::SRGB;
		format = is_srgb ? gpu::TextureFormat::SRGBA : gpu::TextureFormat::RGBA8;
		handle = renderer.createTexture(header.width
			, header.height
			, 1
			, format
			, getGPUFlags() & ~gpu::TextureFlags::SRGB | gpu::TextureFlags::NO_MIPS
			, mem
			, getPath().c_str());
		depth = 1;
		return handle;
	}

	if (header.bitsPerPixel < 24)
	{
		logError("Unsupported color mode ", getPath());
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
	const bool is_srgb = flags & (u32)Flags::SRGB;
	format = is_srgb ? gpu::TextureFormat::SRGBA : gpu::TextureFormat::RGBA8;
	handle = renderer.createTexture(header.width
		, header.height
		, 1
		, format
		, getGPUFlags() & ~gpu::TextureFlags::SRGB | gpu::TextureFlags::NO_MIPS
		, mem
		, getPath().c_str());
	depth = 1;
	return handle;
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

u8* Texture::getLBCInfo(const void* data, gpu::TextureDesc& desc) {
	const LBCHeader* hdr = (const LBCHeader*)data;
	if (hdr->magic != LBCHeader::MAGIC) return nullptr;
	if (hdr->version > 0) return nullptr;

	desc.width = hdr->w;
	desc.height = hdr->h;
	desc.is_cubemap = hdr->flags & LBCHeader::CUBEMAP;
	desc.mips = hdr->mips;
	desc.depth = hdr->slices;
	desc.format = hdr->format;
	return (u8*)data + sizeof(*hdr);
}

static gpu::TextureHandle loadTexture(Renderer& renderer, const gpu::TextureDesc& desc, const Renderer::MemRef& memory, gpu::TextureFlags flags, const char* debug_name)
{
	ASSERT(memory.size > 0);

	const gpu::TextureHandle handle = gpu::allocTextureHandle();
	if (!handle) return handle;

	DrawStream& stream = renderer.getDrawStream();
	if (desc.is_cubemap) flags = flags | gpu::TextureFlags::IS_CUBE;
	if (desc.mips < 2) flags = flags | gpu::TextureFlags::NO_MIPS;
	stream.createTexture(handle, desc.width, desc.height, desc.depth, desc.format, flags, debug_name);
				
	const u8* ptr = (const u8*)memory.data;
	for (u32 layer = 0; layer < desc.depth; ++layer) {
		for(int side = 0; side < (desc.is_cubemap ? 6 : 1); ++side) {
			const u32 z = layer * (desc.is_cubemap ? 6 : 1) + side;
			for (u32 mip = 0; mip < desc.mips; ++mip) {
				const u32 w = maximum(desc.width >> mip, 1);
				const u32 h = maximum(desc.height >> mip, 1);
				const u32 mip_size_bytes = gpu::getSize(desc.format, w, h);
				stream.update(handle, mip, 0, 0, z, w, h, desc.format, ptr, mip_size_bytes);
				ptr += mip_size_bytes;
			}
		}
	}
	ASSERT(memory.own);
	stream.freeMemory(memory.data, renderer.getAllocator());
	return handle;
}

#ifdef LUMIX_BASIS_UNIVERSAL
	static bool loadBasisU(Texture& texture, IInputStream& file)
	{
		if(texture.data_reference > 0) {
			logError("Unsupported texture format ", texture.getPath(), " to access on CPU. Use uncompressed TGA without mipmaps or RAW.");
			return false;
		}
		static bool once = []() { basist::basisu_transcoder_init(); return true; }();
		InputMemoryStream blob(file.getBuffer(), file.size());
		blob.skip(7);
		const gpu::TextureFormat gpu_format = blob.read<gpu::TextureFormat>();
		const u32 size = u32(blob.size() - blob.getPosition());
		const u8* data = (const u8*)blob.skip(size);

		basist::basisu_transcoder transcoder(nullptr);
		if (transcoder.validate_header(data, size)) {
			basist::basisu_image_info info;
			if (transcoder.get_image_info(data, size, info, 0)) {
				basist::basisu_file_info fileInfo;
				transcoder.get_file_info(data, size, fileInfo);
				if (transcoder.start_transcoding(data, size)) {
					gpu::TextureDesc desc;
					desc.width = info.m_width;
					desc.height = info.m_height;
					desc.depth = 1;
					desc.format = gpu_format;
					desc.is_cubemap = false;
					desc.mips = info.m_total_levels;
				
					OutputMemoryStream tmp(texture.allocator);
					u32 blocks = 0;
					for (u32 i = 0; i < info.m_total_levels; ++i) {
						u32 w = maximum(info.m_width >> i, 1);
						u32 h = maximum(info.m_height >> i, 1);
						blocks += (w + 3) / 4 * (h + 3) / 4;
					}
				
					basist::transcoder_texture_format format;
					switch(gpu_format) {
						case gpu::TextureFormat::BC1: format = basist::transcoder_texture_format::cTFBC1; break;
						case gpu::TextureFormat::BC3: format = basist::transcoder_texture_format::cTFBC3; break;
						case gpu::TextureFormat::BC5: format = basist::transcoder_texture_format::cTFBC5; break;
						default: ASSERT(false); break;
					}

					const u32 block_bytes_size = (gpu_format == gpu::TextureFormat::BC1 ? 8 : 16);
					tmp.resize(block_bytes_size * blocks);

					u8* ptr = tmp.getMutableData();
					for (u32 i = 0; i < info.m_total_levels; ++i) {
						u32 w = maximum(info.m_width >> i, 1);
						u32 h = maximum(info.m_height >> i, 1);
						u32 mip_blocks = ((w + 3) / 4) * ((h + 3) / 4);

						transcoder.get_image_level_desc(data, size, 0, i, w, h, mip_blocks);
						if (!transcoder.transcode_image_level(data, size, 0, i, ptr, mip_blocks, format)) return false;
						ptr += mip_blocks * block_bytes_size; 
					}
					
					Renderer::MemRef mem = texture.renderer.copy(tmp.data(), (u32)tmp.size());
					texture.handle = loadTexture(texture.renderer, desc, mem, texture.getGPUFlags(), texture.getPath().c_str());
					if (texture.handle) {
						texture.mips = info.m_total_levels;
						texture.width = desc.width;
						texture.height = desc.height;
						texture.mips = desc.mips;
						texture.depth = desc.depth;
						texture.is_cubemap = desc.is_cubemap;
					}

					return texture.handle;
				}
			}
		}
		return false;
	}
#endif


static bool loadLBC(Texture& texture, const u8* data, u32 size)
{
	gpu::TextureDesc desc;
	const u8* image_data = Texture::getLBCInfo(data, desc);
	if (!image_data) {
		logError("Corrupted or unsupported texture ", texture.getPath());
		return false;
	};

	const u32 offset = u32(image_data - data);
	if (offset >= size) return false;

	if(texture.data_reference > 0) {
		if (desc.format != gpu::TextureFormat::RGBA8) {
			logError("Unsupported texture format ", texture.getPath(), " to access on CPU. Use uncompressed TGA without mipmaps or RAW.");
		}
		else {
			texture.data.resize(size - offset);
			memcpy(texture.data.getMutableData(), image_data, texture.data.size());
		}
	}

	Renderer::MemRef mem = texture.renderer.copy(image_data, size - offset);
	texture.handle = loadTexture(texture.renderer, desc, mem, texture.getGPUFlags(), texture.getPath().c_str());
	if (texture.handle) {
		texture.width = desc.width;
		texture.height = desc.height;
		texture.mips = desc.mips;
		texture.depth = desc.depth;
		texture.is_cubemap = desc.is_cubemap;
		texture.format = desc.format;
	}

	return texture.handle;
}

gpu::TextureFlags Texture::getGPUFlags() const
{
	gpu::TextureFlags gpu_flags = gpu::TextureFlags::NONE;
	if(flags & (u32)Flags::SRGB) {
		gpu_flags = gpu_flags | gpu::TextureFlags::SRGB;
	}
	if(flags & (u32)Flags::POINT) {
		gpu_flags = gpu_flags | gpu::TextureFlags::POINT_FILTER;
	}
	if(flags & (u32)Flags::ANISOTROPIC) {
		gpu_flags = gpu_flags | gpu::TextureFlags::ANISOTROPIC_FILTER;
	}
	if (flags & (u32)Flags::CLAMP_U) {
		gpu_flags = gpu_flags | gpu::TextureFlags::CLAMP_U;
	}
	if (flags & (u32)Flags::CLAMP_V) {
		gpu_flags = gpu_flags | gpu::TextureFlags::CLAMP_V;
	}
	if (flags & (u32)Flags::CLAMP_W) {
		gpu_flags = gpu_flags | gpu::TextureFlags::CLAMP_W;
	}
	return gpu_flags;
}


bool Texture::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();
	profiler::pushString(getPath().c_str());
	
	char ext[4] = {};
	InputMemoryStream file(mem, size);
	if (!file.read(ext, 3)) return false;
	if (!file.read(&flags, sizeof(flags))) return false;

	bool loaded = false;

	#ifdef LUMIX_BASIS_UNIVERSAL
		if (equalIStrings(ext, "bsu")) {
			loaded = loadBasisU(*this, file);
		} else 
	#endif

	if (equalIStrings(ext, "dds")) {
		logWarning("Outdated baked texture ", getPath(), ". Please delete directory .lumix and try again");
		return false;
	}
	
	if (equalIStrings(ext, "lbc")) {
		loaded = loadLBC(*this, (const u8*)file.getBuffer() + file.getPosition(), u32(file.size() - file.getPosition()));
	}
	else if (equalIStrings(ext, "raw")) {
		loaded = loadRaw(*this, file, allocator);
	}
	else {
		loaded = loadTGA(file);
	}
	if (!loaded) {
		logWarning("Error loading texture ", getPath());
		return false;
	}

	return true;
}


void Texture::unload()
{
	if (handle) {
		renderer.getEndFrameDrawStream().destroy(handle);
		handle = gpu::INVALID_TEXTURE;
	}
	data.clear();
}


} // namespace Lumix
