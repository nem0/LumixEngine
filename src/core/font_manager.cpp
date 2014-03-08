#include "core/lux.h"
#include "core/font_manager.h"

#include "core/resource.h"
namespace Lux
{
	class FontResource : public Resource
	{
		friend class FontManager;

	protected:
		FontResource(const Path& path)
			: Resource(path)
		{ }

		~FontResource()
		{ }

		virtual void doLoad(void) LUX_OVERRIDE
		{
//			m_impl->m_font_image = static_cast<OpenGLTexture*>(loadImage(path, file_system));
//			m_impl->m_font_image->onLoaded().bind<OpenGLRendererImpl, &OpenGLRendererImpl::fontImageLoaded>(m_impl);
		}

		virtual void doUnload(void) LUX_OVERRIDE
		{
		}

		virtual void doReload(void) LUX_OVERRIDE
		{
		}
	};

		Resource* FontManager::createResource(const Path& path)
		{
			return LUX_NEW(FontResource)(path);
		}

		void FontManager::destroyResource(Resource& resource)
		{
			LUX_DELETE(static_cast<FontResource*>(&resource));
		}
}