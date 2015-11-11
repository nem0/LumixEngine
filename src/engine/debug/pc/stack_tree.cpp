#include "debug/stack_tree.h"
#include "lumix.h"
#include "core/MT/atomic.h"
#include "core/string.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Dbghelp.h>


#pragma comment(lib, "Dbghelp.lib")


namespace Lumix
{


namespace Debug
{


int StackTree::s_instances = 0;


class StackNode
{
public:
	~StackNode()
	{
		delete m_next;
		delete m_first_child;
	}

	void* m_instruction;
	StackNode* m_next;
	StackNode* m_first_child;
	StackNode* m_parent;
};


StackTree::StackTree()
{
	m_root = nullptr;
	if (MT::atomicIncrement(&s_instances) == 1)
	{
		HANDLE process = GetCurrentProcess();
		SymInitialize(process, nullptr, TRUE);
	}
}


StackTree::~StackTree()
{
	delete m_root;
	if (MT::atomicDecrement(&s_instances) == 0)
	{
		HANDLE process = GetCurrentProcess();
		SymCleanup(process);
	}
}


int StackTree::getPath(StackNode* node, StackNode** output, int max_size)
{
	int i = 0;
	while(i < max_size && node)
	{
		output[i] = node;
		i++;
		node = node->m_parent;
	}
	return i;
}


StackNode* StackTree::getParent(StackNode* node)
{
	return node ? node->m_parent : nullptr;
}


bool StackTree::getFunction(StackNode* node, char* out, int max_size, int* line)
{
	HANDLE process = GetCurrentProcess();
	uint8 symbol_mem[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
	SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_mem);
	memset(symbol_mem, 0, sizeof(symbol_mem));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	BOOL success = SymFromAddr(process, (DWORD64)(node->m_instruction), 0, symbol);
	IMAGEHLP_LINE64 line_info;
	DWORD displacement;
	if (SymGetLineFromAddr64(process, (DWORD64)(node->m_instruction), &displacement, &line_info))
	{
		*line = line_info.LineNumber;
	}
	else
	{
		*line = -1;
	}
	if (success) Lumix::copyString(out, max_size, symbol->Name);

	return success == TRUE;
}


void StackTree::printCallstack(StackNode* node)
{
	while (node)
	{
		HANDLE process = GetCurrentProcess();
		uint8 symbol_mem[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
		SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_mem);
		memset(symbol_mem, 0, sizeof(symbol_mem));
		symbol->MaxNameLen = 255;
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		BOOL success =
			SymFromAddr(process, (DWORD64)(node->m_instruction), 0, symbol);
		if (success)
		{
			IMAGEHLP_LINE line;
			DWORD offset;
			if (SymGetLineFromAddr(
					process, (DWORD64)(node->m_instruction), &offset, &line))
			{
				OutputDebugString("\t");
				OutputDebugString(line.FileName);
				OutputDebugString("(");
				char tmp[20];
				toCString((uint32)line.LineNumber, tmp, sizeof(tmp));
				OutputDebugString(tmp);
				OutputDebugString("):");
			}
			OutputDebugString("\t");
			OutputDebugString(symbol->Name);
			OutputDebugString("\n");
		}
		else
		{ 
			OutputDebugString("\tN/A\n");
		}
		node = node->m_parent;
	}
}


StackNode* StackTree::insertChildren(StackNode* root_node,
									 void** instruction,
									 void** stack)
{
	StackNode* node = root_node;
	while (instruction >= stack)
	{
		StackNode* new_node = new StackNode();
		node->m_first_child = new_node;
		new_node->m_parent = node;
		new_node->m_next = nullptr;
		new_node->m_first_child = nullptr;
		new_node->m_instruction = *instruction;
		node = new_node;
		--instruction;
	}
	return node;
}


StackNode* StackTree::record()
{
	static const int frames_to_capture = 256;
	void* stack[frames_to_capture];
	USHORT captured_frames_count =
		CaptureStackBackTrace(2, frames_to_capture, stack, 0);

	void** ptr = stack + captured_frames_count - 1;
	if (!m_root)
	{
		m_root = new StackNode();
		m_root->m_instruction = *ptr;
		m_root->m_first_child = nullptr;
		m_root->m_next = nullptr;
		m_root->m_parent = nullptr;
		--ptr;
		return insertChildren(m_root, ptr, stack);
	}

	StackNode* node = m_root;
	while (ptr >= stack)
	{
		while (node->m_instruction != *ptr && node->m_next)
		{
			node = node->m_next;
		}
		if (node->m_instruction != *ptr)
		{
			node->m_next = new StackNode;
			node->m_next->m_parent = node->m_parent;
			node->m_next->m_instruction = *ptr;
			node->m_next->m_next = nullptr;
			node->m_next->m_first_child = nullptr;
			--ptr;
			return insertChildren(node->m_next, ptr, stack);
		}
		else if (node->m_first_child)
		{
			--ptr;
			node = node->m_first_child;
		}
		else if (ptr != stack)
		{
			--ptr;
			return insertChildren(node, ptr, stack);
		}
		else
		{
			return node;
		}
	}

	return node;
}


} // namespace Debug


} // namespace Lumix