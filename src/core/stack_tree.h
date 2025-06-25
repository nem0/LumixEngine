#include "arena_allocator.h"
#include "atomic.h"
#include "core.h"
#include "sync.h"


namespace Lumix::debug {

struct StackNode;


struct LUMIX_CORE_API StackTree {
	StackTree(IAllocator& allocator);
	~StackTree();

	StackNode* record();
	void printCallstack(StackNode* node);
	const ArenaAllocator& getAllocator() const { return m_allocator; }
	static bool getFunction(StackNode* node, Span<char> out, int& line);
	static StackNode* getParent(StackNode* node);
	static int getPath(StackNode* node, Span<StackNode*> output);
	static void refreshModuleList();

private:
	StackNode* insertChildren(StackNode* node, void** instruction, void** stack);

	ArenaAllocator m_allocator;
	StackNode* m_root;
	Mutex m_mutex;
	static AtomicI32 s_instances;
};


}