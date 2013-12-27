#pragma once


#include "core/lux.h"
#include "core/string.h"


namespace Lux
{

	namespace FS
	{
		class FileSystem;
	} // ~namespace FS

	namespace UI
	{


		class IRenderer;
		class TextureBase;


		class Atlas
		{
			public:
				struct Part
				{
					void getUvs(float* uvs) const;

					float m_left;
					float m_top;
					float m_right;
					float m_bottom;
					float m_pixel_width;
					float m_pixel_height;
					string name;
				};

			public:
				Atlas() { m_impl = NULL; }

				bool create();
				void destroy();

				void load(IRenderer& renderer, Lux::FS::FileSystem& file_system, const char* filename);
				TextureBase* getTexture() const;
				const Part* getPart(const char* name);
				const string& getPath() const;

			private:
				struct AtlasImpl* m_impl;
		};

	} // ~namespace Lux

} // ~namespace Lux