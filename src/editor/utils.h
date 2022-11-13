#pragma once

#include "engine/delegate.h"
#include "engine/lumix.h"
#include "engine/string.h"
#include "engine/stream.h"

namespace Lumix {

namespace os { enum class Keycode : u8; }

struct LUMIX_EDITOR_API ResourceLocator {
	ResourceLocator(const Span<const char>& path);

	Span<const char> subresource;
	Span<const char> dir;
	Span<const char> basename;
	Span<const char> ext;
	Span<const char> resource;

	Span<const char> full;
};


struct LUMIX_EDITOR_API Action
{
	enum Modifiers : u8 {
		NONE = 0,

		SHIFT = 1 << 0,
		ALT = 1 << 1,
		CTRL = 1 << 2
	};

	Action();
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, os::Keycode key0, Modifiers modifiers, bool is_global);
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, bool is_global);
	bool toolbarButton(struct ImFont* font);
	bool isActive();
	bool shortcutText(Span<char> out) const;

	static bool falseConst() { return false; }

	Modifiers modifiers = Modifiers::NONE;
	os::Keycode shortcut;
	StaticString<32> name;
	StaticString<32> label_short;
	StaticString<64> label_long;
	StaticString<5> font_icon;
	bool is_global;
	void* plugin;
	Delegate<void ()> func;
	Delegate<bool ()> is_selected;
};

inline Action::Modifiers operator |(Action::Modifiers a, Action::Modifiers b) { return Action::Modifiers((u8)a | (u8)b); }
inline void operator |= (Action::Modifiers& a, Action::Modifiers b) { a = a | b; }

LUMIX_EDITOR_API void getShortcut(const Action& action, Span<char> buf);
LUMIX_EDITOR_API void menuItem(Action& a, bool enabled);
LUMIX_EDITOR_API void getEntityListDisplayName(struct StudioApp& app, struct Universe& editor, Span<char> buf, EntityPtr entity);


struct SimpleUndoRedo {
	enum { NO_MERGE_UNDO = 0xffFFffFF };
	struct Undo {
		Undo(IAllocator& allocator) : blob(allocator) {}
		u32 tag;
		OutputMemoryStream blob;
	};

	SimpleUndoRedo(IAllocator& allocator)
		: m_stack(allocator)
		, m_allocator(allocator)
	{}

	bool canUndo() const { return m_stack_idx > 0; }
	bool canRedo() const { return m_stack_idx < m_stack.size() - 1; }

	void undo() {
		if (m_stack_idx <= 0) return;
	
		deserialize(InputMemoryStream(m_stack[m_stack_idx - 1].blob));
		--m_stack_idx;
	}

	void redo() {
		if (m_stack_idx + 1 >= m_stack.size()) return;
	
		deserialize(InputMemoryStream(m_stack[m_stack_idx + 1].blob));
		++m_stack_idx;
	}

	void pushUndo(u32 tag) {
		while (m_stack.size() > m_stack_idx + 1) m_stack.pop();

		Undo u(m_allocator);
		u.tag = tag;
		serialize(u.blob);
		if (tag == NO_MERGE_UNDO || m_stack.back().tag != tag) {
			m_stack.push(static_cast<Undo&&>(u));
			++m_stack_idx;
		}
		else {
			m_stack.back() = static_cast<Undo&&>(u);
		}
	}

	void clearUndoStack() {
		m_stack.clear();
		m_stack_idx = -1;
	}

	virtual void deserialize(InputMemoryStream& blob) = 0;
	virtual void serialize(OutputMemoryStream& blob) = 0;

private:
	IAllocator& m_allocator;
	Array<Undo> m_stack;
	i32 m_stack_idx = -1;
};

} // namespace Lumix
