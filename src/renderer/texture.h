#pragma once


#include "engine/resource.h"
#include "engine/stream.h"
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
#pragma pack()


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
};


struct LUMIX_RENDERER_API Texture final : Resource
{
public: 
	enum class Flags : u32 {
		SRGB = 1 << 0,
		CLAMP_U = 1 << 1,
		CLAMP_V = 1 << 2,
		CLAMP_W = 1 << 3,
		POINT = 1 << 4,
	};

public:
	Texture(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Texture();

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
	u32 getGPUFlags() const;

	static unsigned int compareTGA(IInputStream* file1, IInputStream* file2, int difference, IAllocator& allocator);
	static bool saveTGA(IOutputStream* file,
		int width,
		int height,
		gpu::TextureFormat format,
		const u8* image_dest,
		bool upper_left_origin,
		const Path& path,
		IAllocator& allocator);

	static const ResourceType TYPE;

public:
	u32 width;
	u32 height;
	u32 depth;
	u32 layers;
	u32 mips;
	gpu::TextureFormat format;
	bool is_cubemap;
	u32 flags;
	gpu::TextureHandle handle;
	IAllocator& allocator;
	u32 data_reference;
	OutputMemoryStream data;
	Renderer& renderer;

private:
	void unload() override;
	bool load(u64 size, const u8* mem) override;
	bool loadTGA(IInputStream& file);
};


} // namespace Lumix
