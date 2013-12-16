#pragma once


#include "core/lux.h"


namespace Lux
{

	namespace FS
	{
		class FileSystem;
	} // ~namespace FS

	namespace UI
	{


		class TextureBase;


		class Atlas
		{
			public:
				struct Part
				{
					float m_left;
					float m_top;
					float m_right;
					float m_bottom;
					int m_pixel_width;
					int m_pixel_height;
				};

			public:
				Atlas() { m_impl = NULL; }

				bool create();
				void destroy();

				void load(Lux::FS::FileSystem& file_system, const char* filename);
				const Part* getPart(const char* name);

			private:
				struct AtlasImpl* m_impl;
		};

	} // ~namespace Lux

} // ~namespace Lux