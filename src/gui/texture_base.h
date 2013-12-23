#pragma once


#include "core/string.h"


namespace Lux
{

	namespace UI
	{

		class TextureBase
		{
			public:
				TextureBase(const char* name, float width, float height)
					: m_name(name)
					, m_width(width)
					, m_height(height)
				{}

				virtual ~TextureBase() {}

				const string& getName() const { return m_name; }
				float getWidth() const { return m_width; }
				float getHeight() const { return m_height; }

			protected:
				string m_name;
				float m_width;
				float m_height;
		};

	} // ~namespace Lux

} // ~namespace Lux