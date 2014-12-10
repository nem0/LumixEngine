#pragma once


#include "core/associative_array.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"


namespace Lumix
{

	class Material;


	class LUMIX_ENGINE_API BitmapFontManager : public ResourceManagerBase
	{
		public:
			BitmapFontManager(IAllocator& allocator)
				: ResourceManagerBase(allocator)
				, m_allocator(allocator)
			{}

			~BitmapFontManager() {}

		protected:
			virtual Resource* createResource(const Path& path) override;
			virtual void destroyResource(Resource& resource) override;

		private:
			IAllocator& m_allocator;
	};


	class BitmapFont : public Resource
	{
		public:
			class Character
			{
				public:
					float m_left;
					float m_top;
					float m_right;
					float m_bottom;
					float m_pixel_w;
					float m_pixel_h;
					float m_x_offset;
					float m_y_offset;
					float m_x_advance;
					float m_left_px;
					float m_top_px;
			};

		public:
			BitmapFont(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
			~BitmapFont() { ASSERT(isEmpty()); }

			virtual void doUnload(void) override;
			const Character* getCharacter(char character) const;
			Material* getMaterial() const { return m_material; }

		private:
			virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override;
			void materialLoaded(Resource::State, Resource::State new_state);

		private:
			Material* m_material;
			IAllocator& m_allocator;
			AssociativeArray<char, Character> m_characters;
	};

}