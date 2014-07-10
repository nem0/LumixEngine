#pragma once


#include "core/delegate_list.h"
#include "core/string.h"


namespace Lumix
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
				void setSize(float width, float height) { m_width = width; m_height = height; }
				DelegateList<void (TextureBase&)>& onLoaded() { return m_on_loaded; }

			protected:
				string m_name;
				float m_width;
				float m_height;
				DelegateList<void (TextureBase&)> m_on_loaded;
		};

	} // ~namespace Lumix

} // ~namespace Lumix
