#pragma once


#include "core/lux.h"
#include "core/string.h"


namespace Lux
{
namespace UI
{


	class Block;
	class IRenderer;


	class LUX_GUI_API DecoratorBase LUX_ABSTRACT
	{
		public:
			DecoratorBase(const char* name) : m_name(name) {}
			const char* getName() const { return m_name.c_str(); }
			virtual ~DecoratorBase() {}
			virtual void render(IRenderer& renderer, Block& block) = 0;

		private:
			string m_name;
	};


} // ~namespace UI
} // ~namespace Lux