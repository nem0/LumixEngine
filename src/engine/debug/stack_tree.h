#pragma once


#include "lumix.h"


namespace Lumix
{

namespace Debug
{


	class StackNode;


	class StackTree
	{
		public:
			StackTree();
			~StackTree();

			StackNode* record();
			void printCallstack(StackNode* node);

		private:
			StackNode* insertChildren(StackNode* node, void** instruction, void** stack);

		private:
			StackNode* m_root;
			static int32_t s_instances;
	};


} // namespace Debug


} // namespace Lumix
