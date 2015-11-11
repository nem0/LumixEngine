#pragma once


#include "lumix.h"


namespace Lumix
{

namespace Debug
{


	class StackNode;


	class LUMIX_ENGINE_API StackTree
	{
		public:
			StackTree();
			~StackTree();

			StackNode* record();
			void printCallstack(StackNode* node);
			static bool getFunction(StackNode* node, char* out, int max_size, int* line);
			static StackNode* getParent(StackNode* node);
			static int getPath(StackNode* node, StackNode** output, int max_size);

		private:
			StackNode* insertChildren(StackNode* node, void** instruction, void** stack);

		private:
			StackNode* m_root;
			static int32 s_instances;
	};


} // namespace Debug


} // namespace Lumix
