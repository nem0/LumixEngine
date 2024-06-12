#pragma once


#include "engine/resource.h"
#include "core/stream.h"
#include "core/tag_allocator.h"
#include "gpu/gpu.h"


namespace Lumix
{
struct IInputStream;
struct IOutputStream;
struct Renderer;

#pragma pack(1)
struct TGAHeader
{
	u8 idLength;
	u8 colourMapType;
	u8 dataType;
	u16 colourMapOrigin;
	u16 colourMapLength;
	u8 colourMapDepth;
	u16 xOrigin;
	u16 yOrigin;
	u16 width;
	u16 height;
	u8 bitsPerPixel;
	u8 imageDescriptor;
};

struct LBCHeader {
	static constexpr u32 MAGIC = 'LBC_';
	enum Flags {
		CUBEMAP = 1 << 0,
		IS_3D = 1 << 1
	};
	u32 magic = MAGIC;
	u32 version = 0;
	u32 w = 0;
	u32 h = 0;
	u32 slices = 0;
	u32 mips = 0;
	u32 flags = 0;
	gpu::TextureFormat format;
};

struct RawTextureHeader {
	enum class ChannelType : u32 {
		U8,
		U16,
		FLOAT
	};

	static constexpr u32 LAST_VERSION = 0;
	static constexpr u32 MAGIC = '_LTR';
	u32 magic = MAGIC;
	u32 version = LAST_VERSION;
	u32 width;
	u32 height;
	u32 depth;
	ChannelType channel_type;
	u32 channels_count;
	bool is_array = false;
	u8 padding[3];
};
#pragma pack()

static_assert(sizeof(LBCHeader) == 32);
static_assert(sizeof(RawTextureHeader) == 32);


struct LUMIX_RENDERER_API Texture final : Resource {
	enum class Flags : u32 {
		SRGB = 1 << 0,
		CLAMP_U = 1 << 1,
		CLAMP_V = 1 << 2,
		CLAMP_W = 1 << 3,
		POINT = 1 << 4,
		ANISOTROPIC = 1 << 5
	};

	Texture(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	bool create(u32 w, u32 h, gpu::TextureFormat format, const void* data, u32 size);
	void destroy();

	const u8* getData() const { return data.data(); }
	u8* getData() { return data.getMutableData(); }
	void addDataReference();
	void removeDataReference();
	void onDataUpdated(u32 x, u32 y, u32 w, u32 h);
	void save();
	void setSRGB(bool enable) { setFlags(enable ? flags | u32(Flags::SRGB) : flags & ~u32(Flags::SRGB)); }
	void setFlags(u32 flags);
	bool getFlag(Flags flag);
	void setFlag(Flags flag, bool value);
	u32 getPixelNearest(u32 x, u32 y) const;
	u32 getPixel(float x, float y) const;
	gpu::TextureFlags getGPUFlags() const;

	static u8* getLBCInfo(const void* data, gpu::TextureDesc& desc);
	static bool saveTGA(IOutputStream* file,
		int width,
		int height,
		gpu::TextureFormat format,
		const u8* image_dest,
		bool upper_left_origin,
		const Path& path,
		IAllocator& allocator);

	static const ResourceType TYPE;

	u32 width;
	u32 height;
	u32 depth;
	u32 mips;
	gpu::TextureFormat format;
	bool is_cubemap;
	u32 flags;
	gpu::TextureHandle handle;
	TagAllocator allocator;
	u32 data_reference;
	OutputMemoryStream data;
	Renderer& renderer;

private:
	void unload() override;
	bool load(Span<const u8> mem) override;
};


} // namespace Lumix
