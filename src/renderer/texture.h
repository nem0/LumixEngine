#pragma once


#include "engine/resource.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
namespace FS
{
	class FileSystem;
}


class LUMIX_RENDERER_API Texture LUMIX_FINAL : public Resource
{
	public:
		Texture(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
		~Texture();

		bool create(int w, int h, void* data);
		void destroy();

		const u8* getData() const { return data.empty() ? nullptr : &data[0]; }
		u8* getData() { return data.empty() ? nullptr : &data[0]; }
		void addDataReference();
		void removeDataReference();
		void onDataUpdated(int x, int y, int w, int h);
		void save();
		void setFlags(u32 flags);
		void setFlag(u32 flag, bool value);
		u32 getPixelNearest(int x, int y) const;
		u32 getPixel(float x, float y) const;

		static unsigned int compareTGA(IAllocator& allocator, FS::IFile* file1, FS::IFile* file2, int difference);
		static bool saveTGA(IAllocator& allocator,
			FS::IFile* file,
			int width,
			int height,
			int bytes_per_pixel,
			const u8* image_dest,
			const Path& path);

	public:
		int width;
		int height;
		int bytes_per_pixel;
		int depth;
		int layers;
		int mips;
		bool is_cubemap;
		u32 bgfx_flags;
		bgfx::TextureHandle handle;
		IAllocator& allocator;
		int data_reference;
		Array<u8> data;

	private:
		void unload(void) override;
		bool load(FS::IFile& file) override;
};


} // ~namespace Lumix
