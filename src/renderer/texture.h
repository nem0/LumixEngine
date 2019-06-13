#pragma once


#include "engine/resource.h"
#include "ffr/ffr.h"


namespace Lumix
{
struct IInputStream;
struct IOutputStream;
class Renderer;

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


class LUMIX_RENDERER_API Texture final : public Resource
{
public: 
	enum class Flags : u32 {
		SRGB = 1 << 0,
		CLAMP = 1 << 1
	};

public:
	Texture(const Path& path, Renderer& renderer, ResourceManager& resource_manager, IAllocator& allocator);
	~Texture();

	ResourceType getType() const override { return TYPE; }

	bool create(int w, int h, const void* data, uint size);
	void destroy();

	const u8* getData() const { return data.empty() ? nullptr : &data[0]; }
	u8* getData() { return data.empty() ? nullptr : &data[0]; }
	void addDataReference();
	void removeDataReference();
	void onDataUpdated(int x, int y, int w, int h);
	void save();
	void setSRGB(bool enable) { setFlags(enable ? flags | u32(Flags::SRGB) : flags & ~u32(Flags::SRGB)); }
	void setFlags(u32 flags);
	bool getFlag(Flags flag);
	void setFlag(Flags flag, bool value);
	u32 getPixelNearest(int x, int y) const;
	u32 getPixel(float x, float y) const;
	u32 getFFRFlags() const;

	static unsigned int compareTGA(IInputStream* file1, IInputStream* file2, int difference, IAllocator& allocator);
	static bool saveTGA(IOutputStream* file,
		int width,
		int height,
		int bytes_per_pixel,
		const u8* image_dest,
		const Path& path,
		IAllocator& allocator);

	static const ResourceType TYPE;

public:
	int width;
	int height;
	int bytes_per_pixel;
	int depth;
	int layers;
	int mips;
	bool is_cubemap;
	u32 flags;
	ffr::TextureHandle handle;
	IAllocator& allocator;
	int data_reference;
	Array<u8> data;
	Renderer& renderer;

private:
	void unload() override;
	bool load(u64 size, const u8* mem) override;
	bool loadTGA(IInputStream& file);
};


} // namespace Lumix
