#pragma once


#include "core/string.h"
#include "core/resource.h"
#include "graphics/gl_ext.h"


namespace Lumix
{
namespace FS
{
	class FileSystem;
}


class LUMIX_ENGINE_API Texture : public Resource
{
	public:
		Texture(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		~Texture();

		bool create(int w, int h);
		void apply(int unit = 0);
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		int getBytesPerPixel() const { return m_BPP; }
		const uint8_t* getData() const { return m_data.empty() ? NULL : &m_data[0]; }
		uint8_t* getData() { return m_data.empty() ? NULL : &m_data[0]; }
		void addDataReference();
		void removeDataReference();
		void onDataUpdated();
		void save();
		uint32_t getPixel(float x, float y) const;

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
		bool loadDDS(FS::IFile& file);
		bool loadTGA(FS::IFile& file);
		bool loadRaw(FS::IFile& file);
		void saveTGA();

		virtual void doUnload(void) override;
		virtual FS::ReadCallback getReadCallback() override;

	private:
		IAllocator& m_allocator;
		GLuint m_id;
		int m_width;
		int m_height;
		int m_BPP;
		int m_data_reference;
		Array<uint8_t> m_data;
		bool m_is_cubemap;
};


} // ~namespace Lumix
