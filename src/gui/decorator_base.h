#pragma once


#include "core/lumix.h"
#include "core/string.h"


namespace Lumix
{
namespace UI
{


	class Block;
	class IRenderer;


	class LUX_GUI_API DecoratorBase abstract
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
} // ~namespace Lumix
