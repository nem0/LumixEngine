#pragma once


#include "engine/core/resource.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
namespace FS
{
	class FileSystem;
}


class LUMIX_RENDERER_API Texture : public Resource
{
	public:
		Texture(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		~Texture();

		bool create(int w, int h, void* data);
		void destroy();

		int getDepth() const { return m_depth; }
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		int getBytesPerPixel() const { return m_BPP; }
		const uint8* getData() const { return m_data.empty() ? nullptr : &m_data[0]; }
		uint8* getData() { return m_data.empty() ? nullptr : &m_data[0]; }
		void addDataReference();
		void removeDataReference();
		void onDataUpdated(int x, int y, int w, int h);
		void save();
		void setFlags(uint32 flags);
		void setFlag(uint32 flag, bool value);
		uint32 getFlags() const { return m_flags; }
		uint32 getPixelNearest(int x, int y) const;
		uint32 getPixel(float x, float y) const;
		bgfx::TextureHandle getTextureHandle() const { return m_texture_handle; }

		static bool saveTGA(IAllocator& allocator, FS::IFile* file, int width, int height, int bits_per_pixel, const uint8* data, const Path& path);
		static unsigned int compareTGA(IAllocator& allocator, FS::IFile* file1, FS::IFile* file2, int difference);
		int getAtlasSize() const { return m_atlas_size; }
		void setAtlasSize(int size) { m_atlas_size = size; }

	private:
		bool load3D(FS::IFile& file);
		bool loadDDS(FS::IFile& file);
		bool loadTGA(FS::IFile& file);
		bool loadRaw(FS::IFile& file);
		void saveTGA();

		void unload(void) override;
		bool load(FS::IFile& file) override;

	private:
		IAllocator& m_allocator;
		int m_atlas_size;
		int m_width;
		int m_height;
		int m_BPP;
		int m_depth;
		int m_data_reference;
		uint32 m_flags;
		Array<uint8> m_data;
		bgfx::TextureHandle m_texture_handle;
};


} // ~namespace Lumix
