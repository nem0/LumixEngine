#pragma once


#include "core/lumix.h"


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
			volatile int32_t m_instances = 0;
	};


} // namespace Debug


} // namespace Lumix
