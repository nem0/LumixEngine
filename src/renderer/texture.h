#pragma once


#include "core/resource.h"
#include <bgfx.h>


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
		const uint8_t* getData() const { return m_data.empty() ? nullptr : &m_data[0]; }
		uint8_t* getData() { return m_data.empty() ? nullptr : &m_data[0]; }
		void addDataReference();
		void removeDataReference();
		void onDataUpdated();
		void save();
		void setFlags(uint32_t flags);
		uint32_t getFlags() const { return m_flags; }
		void setPixel(int x, int y, uint32_t color);
		uint32_t getPixelNearest(int x, int y) const;
		uint32_t getPixel(float x, float y) const;
		bgfx::TextureHandle getTextureHandle() const { return m_texture_handle; }

		static bool saveTGA(IAllocator& allocator, FS::IFile* file, int width, int height, int bits_per_pixel, const uint8_t* data, const Path& path);
		static unsigned int compareTGA(IAllocator& allocator, FS::IFile* file1, FS::IFile* file2, int difference);

	private:
		bool load3D(FS::IFile& file);
		bool loadDDS(FS::IFile& file);
		bool loadTGA(FS::IFile& file);
		bool loadRaw(FS::IFile& file);
		void saveTGA();

		virtual void doUnload(void) override;
		virtual void loaded(FS::IFile& file, bool success, FS::FileSystem& fs) override;

	private:
		IAllocator& m_allocator;
		int m_width;
		int m_height;
		int m_BPP;
		int m_depth;
		int m_data_reference;
		uint32_t m_flags;
		Array<uint8_t> m_data;
		bgfx::TextureHandle m_texture_handle;
};


} // ~namespace Lumix
