#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "utils.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/world.h"


namespace Lumix {

namespace LuaTokens {

static inline const u32 token_colors[] = {
	IM_COL32(0xFF, 0x00, 0xFF, 0xff),
	IM_COL32(0xe1, 0xe1, 0xe1, 0xff),
	IM_COL32(0xf7, 0xc9, 0x5c, 0xff),
	IM_COL32(0xFF, 0xA9, 0x4D, 0xff),
	IM_COL32(0xFF, 0xA9, 0x4D, 0xff),
	IM_COL32(0xE5, 0x8A, 0xC9, 0xff),
	IM_COL32(0x93, 0xDD, 0xFA, 0xff),
	IM_COL32(0x67, 0x6b, 0x6f, 0xff),
	IM_COL32(0x67, 0x6b, 0x6f, 0xff)
};

enum class TokenType : u8 {
	EMPTY,
	IDENTIFIER,
	NUMBER,
	STRING,
	STRING_MULTI,
	KEYWORD,
	OPERATOR,
	COMMENT,
	COMMENT_MULTI
};
	
static bool isWordChar(char c) {
	return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_';
}

static bool tokenize(const char* str, u32& token_len, u8& token_type, u8 prev_token_type) {
	static const char* keywords[] = {
		"if",
		"then",
		"else",
		"elseif",
		"end",
		"do",
		"function",
		"repeat",
		"until",
		"while",
		"for",
		"break",
		"return",
		"local",
		"in",
		"not",
		"and",
		"or",
		"goto",
		"self",
		"true",
		"false",
		"nil"
	};

	const char* c = str;
	if (!*c) {
		switch (prev_token_type) {
			case (u8)TokenType::COMMENT_MULTI:
			case (u8)TokenType::STRING_MULTI:
				token_type = prev_token_type;
				break;
			default:
				token_type = (u8)TokenType::EMPTY;
				break;
		}
		token_len = 0;
		return false;
	}

	if (prev_token_type == (u8)TokenType::COMMENT_MULTI) {
		token_type = (u8)TokenType::COMMENT;
		while (*c) {
			if (c[0] == ']' && c[1] == ']') {
				c += 2;
				token_len = u32(c - str);
				return *c;
			}
			++c;
		}
			
		token_type = (u8)TokenType::COMMENT_MULTI;
		token_len = u32(c - str);
		return *c;
	}

	if (prev_token_type == (u8)TokenType::STRING_MULTI) {
		token_type = (u8)TokenType::STRING;
		while (*c) {
			if (c[0] == ']' && c[1] == ']') {
				c += 2;
				token_len = u32(c - str);
				return *c;
			}
			++c;
		}
			
		token_type = (u8)TokenType::STRING_MULTI;
		token_len = u32(c - str);
		return *c;
	}

	if (*c == '[' && c[1] == '[') {
		while (*c) {
			if (c[0] == ']' && c[1] == ']') {
				c += 2;
				token_type = (u8)TokenType::STRING;
				token_len = u32(c - str);
				return *c;
			}
			++c;
		}

		token_type = (u8)TokenType::STRING_MULTI;
		token_len = u32(c - str);
		return false;
	}

	if (*c == '-' && c[1] == '-') {
		if (c[2] == '[' && c[3] == '[') {
			while (*c) {
				if (c[0] == ']' && c[1] == ']') {
					c += 2;
					token_type = (u8)TokenType::COMMENT;
					token_len = u32(c - str);
					return *c;
				}
				++c;
			}
			
			token_type = (u8)TokenType::COMMENT_MULTI;
			token_len = u32(c - str);
			return *c;
		}
		else {
			token_type = (u8)TokenType::COMMENT;
			while (*c) ++c;
			token_len = u32(c - str);
			return *c;
		}
	}

	if (*c == '"') {
		token_type = (u8)TokenType::STRING;
		++c;
		while (*c && *c != '"') ++c;
		if (*c == '"') ++c;
		token_len = u32(c - str);
		return *c;
	}

	if (*c == '\'') {
		token_type = (u8)TokenType::STRING;
		++c;
		while (*c && *c != '\'') ++c;
		if (*c == '\'') ++c;
		token_len = u32(c - str);
		return *c;
	}

	const char operators[] = "*/+-%.<>;=(),:[]{}&|^";
	for (char op : operators) {
		if (*c == op) {
			token_type = (u8)TokenType::OPERATOR;
			token_len = 1;
			return *c;
		}
	}
		
	if (*c >= '0' && *c <= '9') {
		token_type = (u8)TokenType::NUMBER;
		while (*c >= '0' && *c <= '9') ++c;
		token_len = u32(c - str);
		return *c;
	}

	if (*c >= 'a' && *c <= 'z' || *c >= 'A' && *c <= 'Z' || *c == '_') {
		token_type = (u8)TokenType::IDENTIFIER;
		while (isWordChar(*c)) ++c;
		token_len = u32(c - str);
		StringView token_view(str, str + token_len);
		for (const char* kw : keywords) {
			if (equalStrings(kw, token_view)) {
				token_type = (u8)TokenType::KEYWORD;
				break;
			}
		}
		return *c;
	}

	token_type = (u8)TokenType::IDENTIFIER;
	token_len = 1;
	++c;
	return *c;
}

}

// TODO working shortcuts, at least Ctrl-S, Ctrl-O
// TODO horizontal scroll
// TODO utf8
// TODO clipping selection 
// TODO selection should render inclugin "end of line char" in certain cases
// TODO scrollbar
// TODO mouse click - left/right half of character
struct CodeEditorImpl final : CodeEditor {
	struct TextPoint {
		TextPoint() {}
		TextPoint(i32 col, i32 line) : col(col), line(line) {}
		i32 col = 0;
		i32 line = 0;
		bool operator !=(const TextPoint& rhs) const { return col != rhs.col || line != rhs.line; }
		bool operator ==(const TextPoint& rhs) const { return col == rhs.col && line == rhs.line; }
		bool operator < (const TextPoint& rhs) const { return line < rhs.line || line == rhs.line && col < rhs.col; }
		bool operator > (const TextPoint& rhs) const { return line > rhs.line || line == rhs.line && col > rhs.col; }
	};

	struct Cursor : TextPoint {
		Cursor() {}
		Cursor(i32 col, i32 line) : TextPoint(col, line), sel(col, line) {}

		void operator =(const TextPoint& rhs) { col = rhs.col; line = rhs.line; }
		TextPoint sel;

		bool hasSelection() const { return *this != sel; }
		void cancelSelection() { sel = *this; }
	};

	struct Token {
		u32 from;
		u32 len;
		u8 type;
	};

	struct Line {
		Line(IAllocator& allocator) : value(allocator), tokens(allocator) {}
		Line(const String& str, IAllocator& allocator) : value(str, allocator), tokens(allocator) {}
		Line(StringView sv, IAllocator& allocator) : value(sv, allocator), tokens(allocator) {}
		void operator =(const String& rhs) { value = rhs; tokens.clear(); }
		void operator =(String&& rhs) { value = static_cast<String&&>(rhs); }
		u32 length() const { return value.length(); }
		String value;
		Array<Token> tokens;
	};

	struct UndoRecord {
		enum Type {
			BEGIN_GROUP,
			END_GROUP,
			REMOVE,
			INSERT,
			NEW_LINE
		};

		UndoRecord(IAllocator& allocator) : text(allocator), cursors(allocator) {}

		Type type;
		union {
			TextPoint point;
			TextPoint from;
		};
		TextPoint to;
		OutputMemoryStream text;
		Array<Cursor> cursors;

		void execute(CodeEditorImpl& editor, bool is_redo) {
			switch(type) {
				case BEGIN_GROUP:
					if (!is_redo) editor.m_cursors.copyTo(cursors);
					break;
				case END_GROUP:
					if (is_redo) cursors.copyTo(editor.m_cursors);
					else editor.m_cursors.copyTo(cursors);
					break;
				case NEW_LINE: {
					StringView v = editor.m_lines[point.line].value;
					StringView rv = v; rv.removePrefix(point.col);
					StringView lv = v; lv.removeSuffix(v.size() - point.col);

					String r(rv, editor.m_allocator);
					String l(lv, editor.m_allocator);
					editor.m_lines[point.line] = static_cast<String&&>(l);
					editor.m_lines.emplaceAt(point.line + 1, static_cast<String&&>(r), editor.m_allocator);
			
					editor.invalidateTokens(point.line);
					break;
				}
				case INSERT: {
					StringView sv;
					sv.begin = (const char*)text.data();
					sv.end = sv.begin + text.size();
					to = editor.rawInsertText(from, sv);
					editor.invalidateTokens(point.line);
					break;
				}
				case REMOVE: {
					if (!is_redo) editor.serializeText(from, to, text);
					editor.invalidateTokens(from.line);
					editor.rawEraseRange(from ,to);
					break;
				}
			}
		}

		void undo(CodeEditorImpl& editor) {
			switch(type) {
				case BEGIN_GROUP:
					cursors.copyTo(editor.m_cursors);
					editor.ensurePointVisible(editor.m_cursors[0]);
					break;
				case END_GROUP:
					break;
				case NEW_LINE: {
					editor.m_lines[point.line].value.insert(point.col, editor.m_lines[point.line + 1].value.c_str());
					editor.m_lines.erase(point.line + 1);
					editor.invalidateTokens(point.line);
					break;
				}
				case INSERT:
					editor.m_cursors.resize(1);
					editor.rawEraseRange(from, to);
					editor.invalidateTokens(point.line);
					break;
				case REMOVE: {
					StringView sv;
					sv.begin = (const char*)text.data();
					sv.end = (const char*)text.data() + text.size();
					editor.rawInsertText(from, sv);
					editor.invalidateTokens(from.line);
					break;
				}
			}
			cursors.copyTo(editor.m_cursors);
		}
	};

	CodeEditorImpl(StudioApp& app)
		: m_app(app)
		, m_allocator(app.getAllocator(), "code_editor")
		, m_lines(m_allocator)
		, m_cursors(m_allocator)
		, m_undo_stack(m_allocator)
	{
		m_cursors.emplace(Cursor{0, 0});
	}

	void serializeText(TextPoint from, TextPoint to, OutputMemoryStream& blob) {
		u32 size = 0;
		if (from.line == to.line) size = to.col - from.col;
		else {
			size = m_lines[from.line].length() - from.col + 1;
			for (i32 line = from.line + 1; line < to.line - 1; ++line) size += m_lines[line].value.length() + 1/*end of line char*/;
			size += to.col;
		}
		
		blob.reserve(size);
		if (from.line == to.line) {
			const char* str = m_lines[from.line].value.c_str();
			blob.write(str + from.col, to.col - from.col);
		}
		else {
			{
				const char* str = m_lines[from.line].value.c_str();
				blob.write(str + from.col, m_lines[from.line].value.length() - from.col);
				blob.write('\n');
			}
			for (i32 line = from.line + 1; line <= to.line - 1; ++line) {
				const String& str = m_lines[line].value;
				blob.write(str.c_str(), str.length());
				blob.write('\n');
			}
			{
				const char* str = m_lines[to.line].value.c_str();
				blob.write(str, to.col);
			}
		}
	}

	void serializeText(OutputMemoryStream& blob) override {
		u32 size = 0;
		for (const Line& line : m_lines) size += line.value.length() + 1/*end of line char*/;
		blob.reserve(size);
		for (const Line& line : m_lines) {
			blob.write(line.value.c_str(), line.value.length());
			blob.write('\n');
		}
	}

	void setText(StringView text) override {
		m_cursors.clear();
		m_cursors.emplace(Cursor{0, 0});
		m_lines.clear();
		for (StringView line : Lines(text)) {
			if (endsWith(line, "\n")) line.removeSuffix(1);
			if (endsWith(line, "\r")) line.removeSuffix(1);
			m_lines.emplace(line, m_allocator);
		}
		if (m_lines.empty()) m_lines.emplace("", m_allocator);
		m_first_untokenized_line = 0;

		{
			PROFILE_BLOCK("tokenize");
			while (m_first_untokenized_line < m_lines.size()) {
				tokenizeLine();
			}
		}
	}

	void cursorMoved(Cursor& cursor) {
		cursor.line = clamp(cursor.line, 0, m_lines.size() - 1);
		cursor.col = clamp(cursor.col, 0, m_lines[cursor.line].length());
		if (!ImGui::GetIO().KeyShift) {
			cursor.sel.col = cursor.col;
			cursor.sel.line = cursor.line;
		}
		m_blink_timer = 0;
	}

	void moveCursorLeft(Cursor& cursor, bool word) {
		if (word) cursor = getLeftWord(cursor);
		else cursor = getLeft(cursor);
		cursorMoved(cursor);
		if (&cursor == &m_cursors[0]) ensurePointVisible(cursor);
	}

	void moveCursorRight(Cursor& cursor, bool word) {
		if (word) cursor = getRightWord(cursor);
		else cursor = getRight(cursor);
		cursorMoved(cursor);
		if (&cursor == &m_cursors[0]) ensurePointVisible(cursor);
	}

	void ensurePointVisible(TextPoint& cursor, bool center = false) {
		if (center) {
			m_scroll_y = (cursor.line - (m_last_visible_line - m_first_visible_line) / 2) * ImGui::GetTextLineHeight();	
			m_scroll_y = maximum(m_scroll_y, 0.f);
			return;
		}

		if (cursor.line < m_first_visible_line) {
			m_scroll_y -= (m_first_visible_line - cursor.line) * ImGui::GetTextLineHeight(); 
		}

		if (cursor.line > m_last_visible_line - 1 && m_last_visible_line < m_lines.size() - 1) {
			m_scroll_y += (cursor.line - m_last_visible_line + 1) * ImGui::GetTextLineHeight(); 
		}
	}

	void moveCursorUp(Cursor& cursor, u32 line_count = 1) {
		cursor.line = maximum(0, cursor.line - line_count);
		cursorMoved(cursor);
		if (&cursor == &m_cursors[0]) ensurePointVisible(cursor);
	}

	void moveCursorDown(Cursor& cursor, u32 line_count = 1) {
		cursor.line = minimum(m_lines.size() - 1, cursor.line + line_count);
		cursorMoved(cursor);
		if (&cursor == &m_cursors[0]) ensurePointVisible(cursor);
	}

	void moveCursorPageUp(u32 lines_count, float line_height) {
		m_cursors.resize(1);
		i32 old_line = m_cursors[0].line;
		m_cursors[0].line -= lines_count;
		m_scroll_y += (m_cursors[0].line - old_line) * line_height;
		cursorMoved(m_cursors[0]);
	}

	void moveCursorPageDown(u32 lines_count, float line_height) {
		m_cursors.resize(1);
		i32 old_line = m_cursors[0].line;
		m_cursors[0].line += lines_count;
		m_scroll_y += (m_cursors[0].line - old_line) * line_height;
		cursorMoved(m_cursors[0]);
	}

	void moveCursorBegin(Cursor& cursor, bool doc) {
		if (doc) cursor.line = 0;
		if (cursor.col == 0) {
			const String& line = m_lines[cursor.line].value;
			while (cursor.col < (i32)line.length() && !isWordChar(line[cursor.col])) {
				++cursor.col;
			}
		}
		else {
			cursor.col = 0;
		}
		cursorMoved(cursor);
		if (&cursor == &m_cursors[0]) ensurePointVisible(cursor);
	}

	void moveCursorEnd(Cursor& cursor, bool doc) {
		if (doc) cursor.line = m_lines.size() - 1;
		cursor.col = m_lines[cursor.line].length();
		cursorMoved(cursor);
		if (&cursor == &m_cursors[0]) ensurePointVisible(cursor);
	}

	void invalidateTokens(i32 line) {
		m_first_untokenized_line = minimum(line, m_first_untokenized_line);
	}

	void insertNewLine() {
		beginUndoGroup();
		deleteSelections();

		for (Cursor& cursor : m_cursors) {
			UndoRecord& r = pushUndo();
			r.type = UndoRecord::NEW_LINE;
			r.point = cursor;
			r.execute(*this, false);

			i32 line = cursor.line;
			i32 col = cursor.col;
			for (Cursor& c: m_cursors) {
				if (c.line > line) ++c.line;
				else if (c.line == line && c.col >= col) {
					++c.line;
					c.col = c.col - col;
				}
			}
		}

		// we did not adjust sel.line and sel.col while moving cursors, so fix it
		for (Cursor& cursor : m_cursors) {
			cursor.cancelSelection();
		}
		endUndoGroup();
	}

	void deleteSelections() {
		beginUndoGroup();
		for (Cursor& cursor : m_cursors) {
			deleteSelection(cursor);
		}
		endUndoGroup();
	}

	struct Lines {
		Lines(StringView str) : str(str) {}

		struct Iter {
			bool operator != (const Iter& rhs) { 
				return view.begin != rhs.view.begin || view.end != rhs.view.end || end != rhs.end;
			}
			
			void operator ++() {
				view.begin = view.end;
				// TODO '\r'
				while (view.end != end && *view.end != '\n') ++view.end;
				if (view.end != end) ++view.end;
			}

			StringView operator*() const { return view; }
			StringView view;
			const char* end;
		};

		Iter begin() {
			Iter iter;
			iter.view.begin = str.begin;
			iter.view.end = str.begin;
			iter.end = str.end;
			++iter;
			return iter;
		}

		Iter end() {
			Iter iter;
			iter.end = str.end;
			iter.view.begin = iter.view.end = str.end;
			return iter;
		}

		StringView str;
	};

	void rawEraseRange(TextPoint from, TextPoint to) {
		if (from.line == to.line) {
			m_lines[from.line].value.eraseRange(from.col, to.col - from.col);
		}
		else {
			m_lines[from.line].value.resize(from.col);
			m_lines[to.line].value.eraseRange(0, to.col);
			if (to.line - from.line - 1 > 0) m_lines.eraseRange(from.line + 1, to.line - from.line - 1);
			
			m_lines[from.line].value.append(m_lines[from.line + 1].value);
			m_lines.erase(from.line + 1);
		}
	}

	TextPoint rawInsertText(TextPoint p, StringView value) {
		for (StringView line : Lines(value)) {
			if (endsWith(line, "\n")) {
				line.removeSuffix(1);
				if (p.col == 0) {
					m_lines.emplaceAt(p.line, line, m_allocator);
					++p.line;
				}
				else {
					String& line_str = m_lines[p.line].value;
					Line& next_line = m_lines.emplaceAt(p.line + 1, m_allocator);
					StringView sub = line_str;
					sub.removePrefix(p.col);
					next_line.value = sub;

					line_str.resize(p.col);
					line_str.insert(p.col, line);
					++p.line;
					p.col = 0;
				}
			}
			else {
				m_lines[p.line].value.insert(p.col, line);
				p.col += line.size();
			}
		}
		return p;
	}

	void indent(TextPoint from, TextPoint to) {
		beginUndoGroup();
		if (from > to) swap(from, to);
		if (to.col == 0 && to.line != from.line) --to.line;

		for (i32 i = from.line; i <= to.line; ++i) {
			UndoRecord& r = pushUndo();
			r.type = UndoRecord::INSERT;
			r.text.write('\t');
			r.from = TextPoint(0, i);
			r.execute(*this, false);
		}
		endUndoGroup();
	}

	void unindent(TextPoint from, TextPoint to) {
		beginUndoGroup();
		if (from > to) swap(from, to);
		if (to.col == 0 && to.line != from.line) --to.line;
		
		for (i32 i = from.line; i <= to.line; ++i) {
			if (m_lines[i].value.length() == 0) continue;
			if (m_lines[i].value[0] != '\t') continue;
			
			UndoRecord& r = pushUndo();
			r.type = UndoRecord::REMOVE;
			r.from = TextPoint(0, i);
			r.to = TextPoint(1, i);
			r.execute(*this, false);
		}
		endUndoGroup();
	}

	void insertCharacter(u32 character, bool shift) {
		if (character < 0x20 && character != 0x09) return;
		if (character > 0x7f) return;
		
		beginUndoGroup();
		for (Cursor& cursor : m_cursors) {
			if (character == '\t' && (cursor.line != cursor.sel.line || shift)) {
				i32 from_end = m_lines[cursor.line].value.length() - cursor.col;
				i32 from_end_sel = m_lines[cursor.line].value.length() - cursor.sel.col;
				if (shift) unindent(cursor, cursor.sel);
				else indent(cursor, cursor.sel);
				cursor.col = m_lines[cursor.line].value.length() - from_end;
				cursor.sel.col = m_lines[cursor.line].value.length() - from_end_sel;
			}
			else {
				deleteSelection(cursor);
				UndoRecord& r = pushUndo();
				r.type = UndoRecord::INSERT;
				r.text.write((char)character);
				r.from = cursor;
				r.execute(*this, false);
				++cursor.col;
				cursorMoved(cursor);
				cursor.cancelSelection();
			}
		}
		endUndoGroup();
		++m_version;
	}

	static bool isWordChar(char c) {
		return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_';
	}

	Cursor getWord(TextPoint& point) const {
		Cursor cursor;
		cursor = point;
		cursor.sel = point;
		const char* line = m_lines[cursor.line].value.c_str();
		if (!isWordChar(line[cursor.col])) {
			if (cursor.col > 0) cursor.sel.col = cursor.col - 1;
			while (isWordChar(line[cursor.sel.col]) && cursor.sel.col > 0) --cursor.sel.col;
			if (!isWordChar(line[cursor.sel.col]) && cursor.sel.col < cursor.col) ++cursor.sel.col;
			return cursor;
		}

		while (isWordChar(line[cursor.sel.col]) && cursor.sel.col > 0) --cursor.sel.col;
		if (!isWordChar(line[cursor.sel.col])) ++cursor.sel.col;
		while (isWordChar(line[cursor.col])) ++cursor.col;
		return cursor;
	}

	void selectWord(Cursor& cursor) {
		cursor = getWord(cursor);
	}

	Cursor& getBottomCursor() {
		Cursor* bottom = &m_cursors[0];
		for (Cursor& cursor : m_cursors) {
			if (bottom->line < cursor.line || bottom->line == cursor.line && bottom->col < cursor.col) {
				bottom = &cursor;
			}
		}
		return *bottom;
	}

	void addNextOccurence() {
		Cursor& cursor = getBottomCursor();
		if (cursor.hasSelection()) {
			StringView sel_view;
			sel_view.begin = m_lines[cursor.line].value.c_str() + cursor.sel.col;
			sel_view.end = sel_view.begin - cursor.sel.col + cursor.col;

			i32 line = cursor.line;
			while (line < m_lines.size()) {
				StringView line_str = m_lines[line].value;
				if (line == cursor.line) line_str.removePrefix(cursor.col);
				if (const char* found = findInsensitive(line_str, sel_view)) {
					Cursor& new_cursor = m_cursors.emplace();
					new_cursor.line = line;
					new_cursor.sel.line = line;
					new_cursor.sel.col = i32(found - m_lines[line].value.c_str());
					new_cursor.col = new_cursor.sel.col + sel_view.size();
					ensurePointVisible(new_cursor, true);
					return;
				}
				++line;
			}
		}
		else {
			selectWord(cursor);
		}
	}

	void removeCursorAt(i32 col, i32 line) {
		for (i32 i = m_cursors.size() - 1; i >= 0; --i) {
			Cursor& cursor = m_cursors[i];
			if (line < cursor.sel.line) continue;
			if (line > cursor.line) continue;
			if (line == cursor.line && col < cursor.sel.col) continue;
			if (line == cursor.line && col > cursor.col) continue;

			m_cursors.erase(i);
		}
	}

	void deleteSelection(Cursor& cursor) {
		if (!cursor.hasSelection()) return;
		
		TextPoint from = cursor.sel;
		TextPoint to = cursor;
		if (from > to) swap(from, to);
		
		UndoRecord& r = pushUndo();
		r.type = UndoRecord::REMOVE;
		r.from = from;
		r.to = to;
		r.execute(*this, false);

		i32 line = from.line;
		for (Cursor& cursor : m_cursors) {
			if (cursor < from) continue;

			if (cursor.sel.line > to.line) cursor.sel.line -= to.line - from.line;
			else if (cursor.sel.line == to.line) cursor.col -= to.col;
			
			if (cursor.line > to.line) cursor.line -= to.line - from.line;
			else if (cursor.line == to.line) cursor.col -= to.col;
		}

		cursor.line = cursor.sel.line = from.line;
		cursor.col = cursor.sel.col = from.col;
		ensurePointVisible(cursor);
		++m_version;
	}

	char getChar(TextPoint p) const {
		const String& s = m_lines[p.line].value;
		if (p.col == s.length()) return '\n';
		return s[p.col];
	}

	[[nodiscard]] TextPoint getLeftWord(TextPoint point) {
		TextPoint p = getLeft(point);
		bool is_word = isWordChar(getChar(p));
		p = getLeft(p);
		
		while (isWordChar(getChar(p)) == is_word) {
			p = getLeft(p);
			if (p.line == 0 && p.col == 0) return p;
		}
		return getRight(p);
	}

	[[nodiscard]] TextPoint getRightWord(TextPoint point) {
		TextPoint p = getRight(point);
		bool is_word = isWordChar(getChar(p));
		p = getRight(p);
		
		while (isWordChar(getChar(p)) == is_word) {
			p = getRight(p);
			if (p.line == m_lines.size() - 1 && p.col == m_lines.back().length()) return p;
		}
		return p;
	}

	[[nodiscard]] TextPoint getLeft(TextPoint point) {
		TextPoint p = point;
		--p.col;
		if (p.col >= 0) return p;

		--p.line;
		if (p.line < 0) {
			p.line = 0;
			p.col = 0;
		}
		else {
			p.col = m_lines[p.line].length();
		}
		return p;
	}

	[[nodiscard]] TextPoint getRight(TextPoint point) {
		TextPoint p = point;
		++p.col;
		if (p.col <= (i32)m_lines[p.line].length()) return p;
		if (p.line == m_lines.size() - 1) return p;
		++p.line;
		p.col = 0;
		return p;
	}

	void selectToLeft(Cursor& c) {
		if (c.sel < c) c.sel = getLeft(c.sel);
		else c = getLeft(c);
	}

	void selectToRight(Cursor& c, bool word) {
		if (word) {
			if (c.sel > c) c.sel = getRightWord(c.sel);
			else c = getRightWord(c);
		}
		else {
			if (c.sel > c) c.sel = getRight(c.sel);
			else c = getRight(c);
		}
	}

	void del(bool word) {
		beginUndoGroup();
		for (Cursor& cursor : m_cursors) {
			if (!cursor.hasSelection()) selectToRight(cursor, word);
			deleteSelection(cursor);
		}
		endUndoGroup();
	}

	void backspace() {
		beginUndoGroup();
		for (Cursor& cursor : m_cursors) {
			if (!cursor.hasSelection()) selectToLeft(cursor);
			deleteSelection(cursor);
		}
		endUndoGroup();
	}

	void pasteFromClipboard() {
		ImGuiIO& io = ImGui::GetIO();
		if (!io.GetClipboardTextFn) return;

		const char* text = io.GetClipboardTextFn(io.ClipboardUserData);
		u32 len = stringLength(text);
		StringView sv = text;

		beginUndoGroup();
		for (Cursor& cursor : m_cursors) {
			UndoRecord& r = pushUndo();
			r.type = UndoRecord::INSERT;
			r.from = cursor;
			r.text.write(text, len);
			r.execute(*this, false);
		}
		endUndoGroup();
		ensurePointVisible(m_cursors[0]);
	}

	void copyToClipboard() {
		if (m_cursors.size() != 1) return;
		ImGuiIO& io = ImGui::GetIO();
		if (!io.SetClipboardTextFn) return;

		TextPoint from = m_cursors[0];
		TextPoint to = m_cursors[0].sel;
		if (from > to) swap(from, to);
		OutputMemoryStream blob(m_allocator);
		serializeText(from, to, blob);
		blob.write('\0');
		io.SetClipboardTextFn(io.ClipboardUserData, (const char*)blob.data());
	}

	void selectAll() {
		m_cursors.resize(1);
		m_cursors[0] = TextPoint(0, 0);
		m_cursors[0].sel = TextPoint(m_lines.back().length(), m_lines.size() - 1);
	}

	void find(TextPoint from) {
		StringView sel_view = m_search_text;
		if (sel_view.size() == 0) return;

		i32 line = from.line;
		while (line < m_lines.size()) {
			StringView line_str = m_lines[line].value;
			if (line == from.line) line_str.removePrefix(from.col);
			if (const char* found = findInsensitive(line_str, sel_view)) {
				m_cursors.resize(1);
				Cursor& new_cursor = m_cursors[0];
				new_cursor.line = line;
				new_cursor.sel.line = line;
				new_cursor.sel.col = i32(found - m_lines[line].value.c_str());
				new_cursor.col = new_cursor.sel.col + sel_view.size();
				ensurePointVisible(new_cursor, true);
				return;
			}
			++line;
		}
	}

	void guiSearch(const ImVec2& text_area_pos, const ImVec2& text_area_size, ImFont* font) {
		if (!m_search_visible) {
			if (ImGui::IsKeyPressed(ImGuiKey_F3) && m_search_text[0]) {
				find(m_cursors[0]);
			}
			return;
		}

		if (font) ImGui::PushFont(font);
		ImVec2 p = text_area_pos;
		p.x += text_area_size.x - 350;
		p.x = maximum(p.x, text_area_pos.x);
		ImGui::SetCursorScreenPos(p);
		float w = text_area_pos.x + text_area_size.x - p.x;
		float h = ImGui::GetTextLineHeightWithSpacing();
		ImGui::SetCursorScreenPos(p);
		ImGui::SetNextItemWidth(-1);
		if (m_focus_search) ImGui::SetKeyboardFocusHere();
		m_focus_search = false;
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;
		// InputTextWithHint clears the text on escape, se we don't let it
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			m_focus_editor = true;
			m_search_visible = false;
		}
		else if (ImGui::InputTextWithHint("##findtext", ICON_FA_SEARCH " Find Text", m_search_text, sizeof(m_search_text), flags)) {
			find(m_search_from);
		}
		if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_F3)) {
			find(m_cursors[0]);
		}
		if (font) ImGui::PopFont();
	}

	void copy(Span<char> out, const Cursor& cursor) const {
		ASSERT(cursor.line == cursor.sel.line);
		
		StringView sv;
		sv.begin = m_lines[cursor.line].value.c_str() + cursor.sel.col;
		sv.end = m_lines[cursor.line].value.c_str() + cursor.col;
		if (sv.begin > sv.end) swap(sv.begin, sv.end);
		copyString(out, sv);
	}

	bool gui(const char* str_id, const ImVec2& size, ImFont* ui_font) override {
		PROFILE_FUNCTION();
		if (!ImGui::BeginChild(str_id, size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::EndChild();
			return false;
		}
		
		u32 version = m_version;
		ImGuiIO& io = ImGui::GetIO();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 min = ImGui::GetCursorScreenPos();
		ImVec2 content_size = ImGui::GetContentRegionAvail();
		const float line_height = ImGui::GetTextLineHeight();
		const u32 line_num_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const u32 code_color = ImGui::GetColorU32(ImGuiCol_Text);
		const u32 selection_color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
		const float char_width = ImGui::CalcTextSize("x").x;
		const float line_num_width = u32(log10(m_lines.size()) + 1) * char_width + 2 * style.FramePadding.x;

		guiSearch(min, content_size, ui_font);

		ImGuiID id = ImGui::GetID("codeditor");
		ImRect bb = { min, min + content_size };
		ImGui::ItemSize(bb);
		ImGui::ItemAdd(bb, id);
		const bool hovered = ImGui::ItemHoverable(bb, id, 0);
		const bool clicked = hovered && ImGui::IsItemClicked();
		if (m_focus_editor || clicked && !ImGui::IsItemActive()) {
			m_focus_editor = false;
	        ImGuiWindow* window =  ImGui::GetCurrentWindow();
			ImGui::SetActiveID(id, window);
			ImGui::SetFocusID(id, window);
			ImGui::FocusWindow(window);
		}
		if (ImGui::IsItemActive()) {
			if (!io.MouseDown[0]) ImGui::SetItemAllowOverlap(); // because of search gui
			if (io.MouseClicked[0] && !clicked) ImGuiEx::ResetActiveID();
			ImGui::SetShortcutRouting(ImGuiKey_Tab, id);
		}
		if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

		const bool handle_input = ImGui::IsItemActive();
		//dl->AddRectFilled(min, min + ImVec2(line_num_width, content_size.y), ImGui::GetColorU32(ImGuiCol_Border));

		min.x += style.FramePadding.x;
		min.y -= m_scroll_y;
		ImVec2 text_area_pos = min + ImVec2(line_num_width + style.FramePadding.x, 0);

		auto screenToLine = [&](float screen_y) { return clamp(i32((screen_y - text_area_pos.y) / line_height), 0, m_lines.size() - 1); };
		auto screenToCol = [&](float screen_x, i32 line) {
			const char* line_str = m_lines[line].value.c_str();
			const char* c = line_str;
			const float text_area_x = screen_x - text_area_pos.x;
			while (*c) {
				// TODO optimize this
				if (ImGui::CalcTextSize(line_str, c).x > text_area_x) return i32(c - line_str);
				++c;
			}
			return (i32)m_lines[line].length();
		};

		auto textToScreenPos = [&](i32 col, i32 line){
			float y = line * line_height;
			const char* line_str = m_lines[line].value.c_str();
			float x = ImGui::CalcTextSize(line_str, line_str + col).x;
			return text_area_pos + ImVec2(x, y);
		};

		// selection
		for (Cursor& c : m_cursors) {
			if (!c.hasSelection()) continue;

			TextPoint from = c.sel;
			TextPoint to = c;
			if (from > to) {
				swap(from, to);
			}

			{
				const ImVec2 line_pos = textToScreenPos(from.col, from.line);
				const ImVec2 line_max = textToScreenPos(from.line == to.line ? to.col : m_lines[from.line].length(), from.line) + ImVec2(0, line_height);
				dl->AddRectFilled(line_pos, line_max, selection_color);
			}

			for (i32 i = from.line + 1; i < to.line; ++i) {
				ImVec2 line_pos = textToScreenPos(0, i);
				ImVec2 line_max = textToScreenPos(m_lines[i].length(), i) + ImVec2(0, line_height);
				dl->AddRectFilled(line_pos, line_max, selection_color);
			}

			if (to.line > from.line) {
				ImVec2 line_pos = textToScreenPos(0, to.line);
				ImVec2 line_max = textToScreenPos(to.col, to.line) + ImVec2(0, line_height);
				dl->AddRectFilled(line_pos, line_max, selection_color);
			}
		}

		// text
		m_first_visible_line = i32(m_scroll_y / line_height);
		float visible_lines = content_size.y / line_height;
		m_first_visible_line = clamp(m_first_visible_line, 0, m_lines.size() - 1);
		m_last_visible_line = minimum(m_first_visible_line + i32(visible_lines), m_lines.size() - 1);
		
		{
			PROFILE_BLOCK("tokenize");
			while (m_first_untokenized_line <= minimum(m_last_visible_line, m_lines.size() - 1)) tokenizeLine();
		}

		u32 visible_tokens = 0;
		for (int j = m_first_visible_line; j <= m_last_visible_line; ++j) {
			float line_offset_y = j * line_height;
			ImVec2 line_pos = min + ImVec2(0, line_offset_y);
			StaticString<16> line_num_str(j + 1);
			dl->AddText(line_pos, line_num_color, line_num_str);
			const char* str = m_lines[j].value.c_str();
			ImVec2 p = text_area_pos + ImVec2(0, line_offset_y);
			for (const Token& t : m_lines[j].tokens) {
				dl->AddText(p, m_token_colors[t.type], str + t.from, str + t.from + t.len);
				p.x += ImGui::CalcTextSize(str + t.from, str + t.from + t.len).x;
				++visible_tokens;
			}
		}
		profiler::pushInt("Num tokens", visible_tokens);

		// cursors
		m_blink_timer += io.DeltaTime;
		m_blink_timer = fmodf(m_blink_timer, 1.f);
		bool draw_cursors = m_blink_timer < 0.6f;
		if (handle_input) {
			for (i32 i = 0; i < m_cursors.size(); ++i) {
				Cursor& c = m_cursors[i];
				ImVec2 cursor_pos = textToScreenPos(c.col, c.line);
				if (draw_cursors) dl->AddRectFilled(cursor_pos, cursor_pos + ImVec2(1, line_height), code_color);
				if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) moveCursorLeft(c, io.KeyCtrl);
				else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) moveCursorRight(c, io.KeyCtrl);
				else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveCursorUp(c);
				else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveCursorDown(c);
				else if (ImGui::IsKeyPressed(ImGuiKey_End)) moveCursorEnd(c, io.KeyCtrl);
				else if (ImGui::IsKeyPressed(ImGuiKey_Home)) moveCursorBegin(c, io.KeyCtrl);
			}
	        const ImGuiInputFlags f_repeat = ImGuiInputFlags_Repeat;
			
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_cursors.resize(1);
			else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) del(io.KeyCtrl);
			else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) backspace();
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_Z, id, f_repeat)) undo();
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z, id, f_repeat)) redo();
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_C, id)) copyToClipboard();
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_X, id)) { copyToClipboard(); deleteSelections(); }
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_V, id, f_repeat)) pasteFromClipboard();
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_A, id)) selectAll();
			else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) insertNewLine();
			else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) moveCursorPageUp(u32(content_size.y / line_height + 1), line_height);
			else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) moveCursorPageDown(u32(content_size.y / line_height + 1), line_height);
			else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_F, id)) {
				m_search_visible = true;
				m_focus_search = true;
				m_search_from = m_cursors[0];
				if (m_cursors[0].hasSelection() && m_cursors[0].line == m_cursors[0].sel.line) copy(Span(m_search_text), m_cursors[0]);
				else copy(Span(m_search_text), getWord(m_cursors[0]));
			}
			
			// mouse input
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				selectWord(m_cursors[0]);
			}
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				const i32 line = screenToLine(io.MousePos.y);
				const i32 col = screenToCol(io.MousePos.x, line);
				removeCursorAt(col, line);
				Cursor& cursor = io.KeyAlt ? m_cursors.emplace() : (m_cursors.resize(1), m_cursors.back());
				cursor.line = line;
				cursor.col = col;
				cursorMoved(cursor);
			}

			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_cursors.size() == 1) {
				m_cursors[0].line = screenToLine(io.MousePos.y);
				m_cursors[0].col = screenToCol(io.MousePos.x, m_cursors[0].line);
			}

			m_scroll_y -= io.MouseWheel * line_height * 5;
			m_scroll_y = maximum(0.f, m_scroll_y);

			// text input
			if (io.KeyCtrl && !io.KeyAlt) {
				if (ImGui::IsKeyPressed(ImGuiKey_D)) addNextOccurence();
			}
		
			const bool ignore_char_inputs = (io.KeyCtrl && !io.KeyAlt);
			if (!ignore_char_inputs && io.InputQueueCharacters.Size > 0) {
				for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
					u32 character = (u32)io.InputQueueCharacters[n];
					insertCharacter(character, io.KeyShift);
				}

				io.InputQueueCharacters.resize(0);
			}
		}

		ImGui::EndChild();
		return version != m_version;
	}

	void setTokenizer(Tokenizer tokenizer) override {
		m_tokenizer = tokenizer;
	}

	void setTokenColors(Span<const u32> colors) override {
		m_token_colors = colors;
	}

	void tokenizeLine() {
		if (m_first_untokenized_line >= m_lines.size()) return;

		Line& line = m_lines[m_first_untokenized_line];
		line.tokens.clear();

		const char* start = line.value.c_str();
		const char* c = start;
		
		u8 prev_token_type = 0xff;
		if (m_first_untokenized_line > 0 && !m_lines[m_first_untokenized_line - 1].tokens.empty()) {
			prev_token_type = m_lines[m_first_untokenized_line - 1].tokens.back().type;
		}

		bool more_tokens = true;
		while (more_tokens) {
			Token& token = line.tokens.emplace();
			token.from = u32(c - start);
			more_tokens = m_tokenizer(c, token.len, token.type, prev_token_type);
			c += token.len;
			prev_token_type = token.type;
		}

		++m_first_untokenized_line;
	}

	void beginUndoGroup() {
		UndoRecord& r =  pushUndo();
		r.type = UndoRecord::BEGIN_GROUP;
		r.execute(*this, false);
	}

	void endUndoGroup() {
		UndoRecord& r =  pushUndo();
		r.type = UndoRecord::END_GROUP;
		r.execute(*this, false);
	}

	UndoRecord& pushUndo() {
		while (m_undo_stack.size() > m_undo_stack_idx + 1) m_undo_stack.pop();

		++m_undo_stack_idx;
		return m_undo_stack.emplace(m_allocator);
	}

	void undo() {
		if (m_undo_stack_idx < 0) return;
		
		u32 depth = 0;
		do {
			switch (m_undo_stack[m_undo_stack_idx].type) {
				case UndoRecord::BEGIN_GROUP: --depth; break;
				case UndoRecord::END_GROUP: ++depth; break;
				default: break;
			}
			m_undo_stack[m_undo_stack_idx].undo(*this);
			--m_undo_stack_idx;
		} while (depth != 0);
	}

	void redo() {
		if (m_undo_stack_idx + 1 >= m_undo_stack.size()) return;

		u32 depth = 0;
		do {
			++m_undo_stack_idx;
			m_undo_stack[m_undo_stack_idx].execute(*this, true);
			switch (m_undo_stack[m_undo_stack_idx].type) {
				case UndoRecord::BEGIN_GROUP: ++depth; break;
				case UndoRecord::END_GROUP: --depth; break;
				default: break;
			}
		} while(depth != 0);
	}

	i32 m_first_untokenized_line = 0;
	TagAllocator m_allocator; 
	float m_blink_timer = 0;
	StudioApp& m_app;
	Array<Line> m_lines;
	float m_scroll_y = 0;
	Array<Cursor> m_cursors;
	i32 m_first_visible_line = 0;
	i32 m_last_visible_line = 0;
	Tokenizer m_tokenizer = nullptr;
	Span<const u32> m_token_colors;
	u32 m_version = 0;
	Array<UndoRecord> m_undo_stack;
	i32 m_undo_stack_idx = -1;
	
	bool m_focus_search = false;
	bool m_search_visible = false;
	bool m_focus_editor = false;
	char m_search_text[64] = "";
	TextPoint m_search_from;
};

UniquePtr<CodeEditor> createCodeEditor(StudioApp& app) {
	UniquePtr<CodeEditorImpl> editor = UniquePtr<CodeEditorImpl>::create(app.getAllocator(), app);
	return editor.move();
}

UniquePtr<CodeEditor> createLuaCodeEditor(StudioApp& app) {
	UniquePtr<CodeEditorImpl> editor = UniquePtr<CodeEditorImpl>::create(app.getAllocator(), app);
	editor->setTokenColors(LuaTokens::token_colors);
	editor->setTokenizer(&LuaTokens::tokenize);
	return editor.move();
}

ResourceLocator::ResourceLocator(StringView path) {
	full = path;
	const char* c = path.begin;
	subresource.begin = c;
	while(c != path.end && *c != ':') {
		++c;
	}
	if(c != path.end) {
		subresource.end = c;
		dir.begin = c + 1;
	}
	else {
		subresource.end = subresource.begin;
		dir.begin = path.begin;
	}
	
	ext.end = path.end;
	ext.begin = reverseFind(StringView(dir.begin, ext.end), '.');
	if (ext.begin) {
		basename.end = ext.begin;
		++ext.begin;
	}
	else {
		ext.begin = ext.end;
		basename.end = path.end;
	}
	basename.begin = reverseFind(StringView(dir.begin, basename.end), '/');
	if (!basename.begin) basename.begin = reverseFind(StringView(dir.begin, basename.end), '\\');
	if (basename.begin)  {
		dir.end = basename.begin;
		++basename.begin;
	}
	else {
		basename.begin = dir.begin;
		dir.end = dir.begin;
	}
	resource.begin = dir.begin;
	resource.end = ext.end;
}

Action::Action() {
	shortcut = os::Keycode::INVALID;
}

void Action::init(const char* label_short, const char* label_long, const char* name, const char* font_icon, Type type) {
	this->label_long = label_long;
	this->label_short = label_short;
	this->font_icon = font_icon;
	this->name = name;
	this->type = type;
	shortcut = os::Keycode::INVALID;
	is_selected.bind<falseConst>();
}


void Action::init(const char* label_short,
	const char* label_long,
	const char* name,
	const char* font_icon,
	os::Keycode shortcut,
	Modifiers modifiers,
	Type type)
{
	this->label_long = label_long;
	this->label_short = label_short;
	this->name = name;
	this->font_icon = font_icon;
	this->type = type;
	this->shortcut = shortcut;
	this->modifiers = modifiers;
	is_selected.bind<falseConst>();
}

bool Action::shortcutText(Span<char> out) const {
	if (shortcut == os::Keycode::INVALID && modifiers == 0) {
		copyString(out, "");
		return false;
	}
	char tmp[32];
	os::getKeyName(shortcut, Span(tmp));
	
	copyString(out, "");
	if (modifiers & (u8)Action::Modifiers::CTRL) catString(out, "Ctrl ");
	if (modifiers & (u8)Action::Modifiers::SHIFT) catString(out, "Shift ");
	if (modifiers & (u8)Action::Modifiers::ALT) catString(out, "Alt ");
	catString(out, shortcut == os::Keycode::INVALID ? "" : tmp);
	const i32 len = stringLength(out.m_begin);
	if (len > 0 && out[len - 1] == ' ') {
		out[len - 1] = '\0';
	}
	return true;
}

bool Action::toolbarButton(ImFont* font)
{
	const ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
	const ImVec4 bg_color = is_selected.invoke() ? col_active : ImGui::GetStyle().Colors[ImGuiCol_Text];

	if (!font_icon[0]) return false;

	ImGui::SameLine();
	if(ImGuiEx::ToolbarButton(font, font_icon, bg_color, label_long)) {
		func.invoke();
		return true;
	}
	return false;
}


bool Action::isActive() const 
{
	if (ImGui::IsAnyItemFocused()) return false;
	if (shortcut == os::Keycode::INVALID && modifiers == 0) return false;

	if (shortcut != os::Keycode::INVALID && !os::isKeyDown(shortcut)) return false;
	
	Modifiers pressed_modifiers = Modifiers::NONE;
	if (os::isKeyDown(os::Keycode::ALT)) pressed_modifiers |= Modifiers::ALT;
	if (os::isKeyDown(os::Keycode::SHIFT)) pressed_modifiers |= Modifiers::SHIFT;
	if (os::isKeyDown(os::Keycode::CTRL)) pressed_modifiers |= Modifiers::CTRL;
	if (modifiers != pressed_modifiers && modifiers != Modifiers::NONE) return false;

	return true;
}

void getShortcut(const Action& action, Span<char> buf) {
	buf[0] = 0;
		
	if (action.modifiers & (u8)Action::Modifiers::CTRL) catString(buf, "CTRL ");
	if (action.modifiers & (u8)Action::Modifiers::SHIFT) catString(buf, "SHIFT ");
	if (action.modifiers & (u8)Action::Modifiers::ALT) catString(buf, "ALT ");

	if (action.shortcut != os::Keycode::INVALID) {
		char tmp[64];
		os::getKeyName(action.shortcut, Span(tmp));
		if (tmp[0] == 0) return;
		catString(buf, tmp);
	}
}

bool menuItem(const Action& a, bool enabled) {
	char buf[20];
	getShortcut(a, Span(buf));
	return ImGuiEx::MenuItemEx(a.label_short, a.font_icon, buf, a.is_selected.invoke(), enabled);
}

void getEntityListDisplayName(StudioApp& app, World& world, Span<char> buf, EntityPtr entity)
{
	if (!entity.isValid())
	{
		buf[0] = '\0';
		return;
	}

	EntityRef e = (EntityRef)entity;
	const char* name = world.getEntityName(e);
	static const auto MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
	if (world.hasComponent(e, MODEL_INSTANCE_TYPE))
	{
		RenderInterface* render_interface = app.getRenderInterface();
		const Path path = render_interface->getModelInstancePath(world, e);
		if (!path.isEmpty())
		{
			const char* c = path.c_str();
			while (*c && *c != ':') ++c;
			if (*c == ':') {
				copyString(buf, StringView(path.c_str(), u32(c - path.c_str() + 1)));
				return;
			}

			copyString(buf, path);
			StringView basename = Path::getBasename(path);
			if (name && name[0] != '\0')
				copyString(buf, name);
			else
				toCString(entity.index, buf);

			catString(buf, " - ");
			catString(buf, basename);
			return;
		}
	}

	if (name && name[0] != '\0')
	{
		copyString(buf, name);
	}
	else
	{
		toCString(entity.index, buf);
	}
}

static int inputTextCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		String* str = (String*)data->UserData;
		ASSERT(data->Buf == str->c_str());
		str->resize(data->BufTextLen);
		data->Buf = (char*)str->c_str();
	}
	return 0;
}

bool inputStringMultiline(const char* label, String* value, const ImVec2& size) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput;
	return ImGui::InputTextMultiline(label, (char*)value->c_str(), value->length() + 1, size, flags, inputTextCallback, value);
}

bool inputString(const char* label, String* value) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
	return ImGui::InputText(label, (char*)value->c_str(), value->length() + 1, flags, inputTextCallback, value);
}

bool inputString(const char* str_id, const char* label, String* value) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
	ImGuiEx::Label(label);
	return ImGui::InputText(str_id, (char*)value->c_str(), value->length() + 1, flags, inputTextCallback, value);
}

bool inputRotation(const char* label, Quat* value) {
	Vec3 euler = value->toEuler();
	if (ImGuiEx::InputRotation(label, &euler.x)) {
		value->fromEuler(euler);
		return true;
	}
	return false;
}

SimpleUndoRedo::SimpleUndoRedo(IAllocator& allocator)
	: m_stack(allocator)
	, m_allocator(allocator)
{}

bool SimpleUndoRedo::canUndo() const { return m_stack_idx > 0; }
bool SimpleUndoRedo::canRedo() const { return m_stack_idx < m_stack.size() - 1; }

void SimpleUndoRedo::undo() {
	if (m_stack_idx <= 0) return;

	InputMemoryStream blob(m_stack[m_stack_idx - 1].blob);
	deserialize(blob);
	--m_stack_idx;
}

void SimpleUndoRedo::redo() {
	if (m_stack_idx + 1 >= m_stack.size()) return;

	InputMemoryStream blob(m_stack[m_stack_idx + 1].blob);
	deserialize(blob);
	++m_stack_idx;
}

void SimpleUndoRedo::pushUndo(u32 tag) {
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

void SimpleUndoRedo::clearUndoStack() {
	m_stack.clear();
	m_stack_idx = -1;
}

void FileSelector::fillSubitems() {
	m_subdirs.clear();
	m_subfiles.clear();
	FileSystem& fs = m_app.getEngine().getFileSystem();
	const char* base_path = fs.getBasePath();
	
	const Path path(base_path, "/", m_current_dir);
	os::FileIterator* iter = os::createFileIterator(path, m_app.getAllocator());
	os::FileInfo info;
	const char* ext = m_accepted_extension.c_str();
	while (os::getNextFile(iter, &info)) {
		if (equalStrings(info.filename, ".")) continue;
		if (equalStrings(info.filename, "..")) continue;
		if (equalStrings(info.filename, ".lumix") && m_current_dir.length() == 0) continue;

		if (info.is_directory) {
			m_subdirs.emplace(info.filename, m_app.getAllocator());
		}
		else {
			if (!ext[0] || Path::hasExtension(info.filename, ext)) {
				m_subfiles.emplace(info.filename, m_app.getAllocator());
			}
		}
	}
	os::destroyFileIterator(iter);
}


bool FileSelector::breadcrumb(StringView path) {
	if (path.empty()) {
		if (ImGui::Button(".")) {
			m_current_dir = "";
			fillSubitems();
			return true;
		}
		return false;
	}

	if (path.back() == '/') path.removeSuffix(1);
	
	StringView dir = Path::getDir(path);
	StringView basename = Path::getBasename(path);
	if (breadcrumb(dir)) return true;
	ImGui::SameLine();
	ImGui::TextUnformatted("/");
	ImGui::SameLine();
	
	char tmp[MAX_PATH];
	copyString(Span(tmp), basename);
	if (ImGui::Button(tmp)) {
		m_current_dir = String(path, m_app.getAllocator());
		fillSubitems();
		return true;
	}
	return false;
}

FileSelector::FileSelector(const char* ext, StudioApp& app)
	: m_app(app)
	, m_filename(app.getAllocator())
	, m_full_path(app.getAllocator())
	, m_current_dir(app.getAllocator())
	, m_subdirs(app.getAllocator())
	, m_subfiles(app.getAllocator())
	, m_accepted_extension(ext, app.getAllocator())
{
	fillSubitems();
}

DirSelector::DirSelector(StudioApp& app)
	: m_app(app)
	, m_current_dir(app.getAllocator())
	, m_subdirs(app.getAllocator())
{}


void DirSelector::fillSubitems() {
	m_subdirs.clear();
	FileSystem& fs = m_app.getEngine().getFileSystem();
	const char* base_path = fs.getBasePath();
	
	const Path path(base_path, "/", m_current_dir);
	os::FileIterator* iter = os::createFileIterator(path, m_app.getAllocator());
	os::FileInfo info;
	while (os::getNextFile(iter, &info)) {
		if (equalStrings(info.filename, ".")) continue;
		if (equalStrings(info.filename, "..")) continue;
		if (equalStrings(info.filename, ".lumix") && m_current_dir.length() == 0) continue;

		if (info.is_directory) {
			m_subdirs.emplace(info.filename, m_app.getAllocator());
		}
	}
	os::destroyFileIterator(iter);
}

bool DirSelector::breadcrumb(StringView path) {
	if (path.empty()) {
		if (ImGui::Button(".")) {
			m_current_dir = "";
			fillSubitems();
			return true;
		}
		return false;
	}
	if (path.back() == '/') path.removeSuffix(1);
	
	StringView dir = Path::getDir(path);
	StringView basename = Path::getBasename(path);
	if (breadcrumb(dir)) return true;
	ImGui::SameLine();
	ImGui::TextUnformatted("/");
	ImGui::SameLine();
	
	char tmp[MAX_PATH];
	copyString(Span(tmp), basename);
	if (ImGui::Button(tmp)) {
		m_current_dir = String(path, m_app.getAllocator());
		fillSubitems();
		return true;
	}
	return false;
}

bool DirSelector::gui(const char* label, bool* open) {
	if (*open && !ImGui::IsPopupOpen(label)) {
		ImGui::OpenPopup(label);
		fillSubitems();
	}

	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		bool recently_open_create_folder = false;
		if (ImGui::Button(ICON_FA_PLUS " Create folder")) {
			m_creating_folder = true;
			m_new_folder_name[0] = '\0';
			recently_open_create_folder = true;
		}
		breadcrumb(m_current_dir);
		if (ImGui::BeginChild("list", ImVec2(300, 300), true, ImGuiWindowFlags_NoScrollbar)) {
			if (m_current_dir.length() > 0) {
				if (ImGui::Selectable(ICON_FA_LEVEL_UP_ALT "..", false, ImGuiSelectableFlags_DontClosePopups)) {
					StringView dir = Path::getDir(m_current_dir);
					if (!dir.empty()) dir.removeSuffix(1);
					m_current_dir = String(dir, m_app.getAllocator());
					fillSubitems();
				}
			}
			
			if (m_creating_folder) {
				ImGui::SetNextItemWidth(-1);
				if (recently_open_create_folder) ImGui::SetKeyboardFocusHere();
				ImGui::InputTextWithHint("##nf", "New folder name", m_new_folder_name, sizeof(m_new_folder_name), ImGuiInputTextFlags_AutoSelectAll);
				if (ImGui::IsItemDeactivated()) {
					m_creating_folder = false;
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						if (m_new_folder_name[0]) {
							FileSystem& fs = m_app.getEngine().getFileSystem();
							const Path fullpath(fs.getBasePath(), m_current_dir, "/", m_new_folder_name);
							if (!os::makePath(fullpath.c_str())) {
								logError("Failed to create ", fullpath);
							}
							else {
								m_current_dir.append("/", m_new_folder_name); 
								m_new_folder_name[0] = '\0';
							}
							fillSubitems();
						}
					}
				}
			}

			for (const String& subdir : m_subdirs) {
				ImGui::TextUnformatted(ICON_FA_FOLDER); ImGui::SameLine();
				if (ImGui::Selectable(subdir.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
					m_current_dir.append("/", subdir.c_str());
					fillSubitems();
					break;
				}
			}
		}
		ImGui::EndChild();
	
		bool res = ImGui::Button(ICON_FA_CHECK " Select");
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TIMES " Cancel")) ImGui::CloseCurrentPopup();
	
		if (res) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		if (!ImGui::IsPopupOpen(label)) *open = false;
		return res;
	}
	return false;
}

FileSelector::FileSelector(StudioApp& app)
	: m_app(app)
	, m_filename(app.getAllocator())
	, m_full_path(app.getAllocator())
	, m_current_dir(app.getAllocator())
	, m_subdirs(app.getAllocator())
	, m_subfiles(app.getAllocator())
	, m_accepted_extension(app.getAllocator())
{}

const char* FileSelector::getPath() {
	if (Path::getExtension(m_full_path).empty()) m_full_path.append(".", m_accepted_extension.c_str());
	return m_full_path.c_str();
}

bool FileSelector::gui(bool show_breadcrumbs, const char* accepted_extension) {
	if (m_accepted_extension != accepted_extension) {
		m_accepted_extension = accepted_extension;
		fillSubitems();
	}

	bool res = false;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Filename"); ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
	bool changed = inputString("##fn", &m_filename);
	if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter)) res = true;

	if (show_breadcrumbs) {
		changed = breadcrumb(m_current_dir) || changed;
	}
	if (ImGui::BeginChild("list", ImVec2(300, 300), true, ImGuiWindowFlags_NoScrollbar)) {
		if (m_current_dir.length() > 0) {
			if (ImGui::Selectable(ICON_FA_LEVEL_UP_ALT "..", false, ImGuiSelectableFlags_DontClosePopups)) {
				StringView dir = Path::getDir(m_current_dir);
				if (!dir.empty()) dir.removeSuffix(1);
				m_current_dir = String(dir, m_app.getAllocator());
				fillSubitems();
				changed = true;
			}
		}
		
		for (const String& subdir : m_subdirs) {
			ImGui::TextUnformatted(ICON_FA_FOLDER); ImGui::SameLine();
			if (ImGui::Selectable(subdir.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
				m_current_dir.append("/", subdir.c_str());
				fillSubitems();
				changed = true;
				break;
			}
		}
		
		for (const String& subfile : m_subfiles) {
			if (ImGui::Selectable(subfile.c_str(), false, ImGuiSelectableFlags_DontClosePopups | ImGuiSelectableFlags_AllowDoubleClick)) {
				m_filename = subfile;
				changed = true;
				if (ImGui::IsMouseDoubleClicked(0)) {
					res = true;
				}
			}
		}
	}
	ImGui::EndChild();
	if (changed) {
		m_full_path = m_current_dir;
		m_full_path.append("/", m_filename.c_str());
	}
	return res;
}

bool FileSelector::gui(const char* label, bool* open, const char* extension, bool save) {
	if (*open && !ImGui::IsPopupOpen(label)) {
		ImGui::OpenPopup(label);
		m_save = save;
		m_accepted_extension = extension;
		m_filename = "";
		m_full_path = "";
		fillSubitems();
	}

	bool res = false;
	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		res = gui(true, extension);
	
		if (m_save) {
			if (ImGui::Button(ICON_FA_SAVE " Save")) {
				if (!Path::hasExtension(m_full_path, m_accepted_extension)) {
					m_full_path.append(".", m_accepted_extension.c_str());
				}
				if (m_app.getEngine().getFileSystem().fileExists(m_full_path)) {
					ImGui::OpenPopup("warn_overwrite");
				}
				else {
					res = true;
				}
			}
		}
		else {
			if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open")) {
				if (m_app.getEngine().getFileSystem().fileExists(m_full_path)) {
					res = true;
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TIMES " Cancel")) ImGui::CloseCurrentPopup();
	
		if (ImGui::BeginPopup("warn_overwrite")) {
			ImGui::TextUnformatted("File already exists, are you sure you want to overwrite it?");
			if (ImGui::Selectable("Yes")) res = true;
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}
		if (res) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		if (!ImGui::IsPopupOpen(label)) *open = false;
		return res;
	}
	return false;
}

enum { OUTPUT_FLAG = 1 << 31 };

NodeEditor::NodeEditor(IAllocator& allocator)
	: SimpleUndoRedo(allocator)
{}

void NodeEditor::splitLink(const NodeEditorNode* node, Array<NodeEditorLink>& links, u32 link_idx) {
	if (node->hasInputPins() && node->hasOutputPins()) {
		NodeEditorLink& new_link = links.emplace();
		NodeEditorLink& link = links[link_idx];
		new_link.color = link.color;
		new_link.to = link.to;
		new_link.from = node->m_id;
		link.to = node->m_id;
		pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
	}
}

void NodeEditor::nodeEditorGUI(Span<NodeEditorNode*> nodes, Array<NodeEditorLink>& links) {
	m_canvas.begin();

	ImGuiEx::BeginNodeEditor("node_editor", &m_offset);
	const ImVec2 origin = ImGui::GetCursorScreenPos();

	ImGuiID moved = 0;
	ImGuiID unlink_moved = 0;
	u32 moved_count = 0;
	u32 unlink_moved_count = 0;
	for (NodeEditorNode* node : nodes) {
		const ImVec2 old_pos = node->m_pos;
		if (node->nodeGUI()) {
			pushUndo(node->m_id);
		}
		if (ImGui::IsItemHovered()) {
			if (ImGui::IsMouseDragging(0)) m_dragged_node = node->m_id;
			else if (ImGui::IsMouseDoubleClicked(0)) onNodeDoubleClicked(*node);
		}
		if (old_pos.x != node->m_pos.x || old_pos.y != node->m_pos.y) {
			moved = node->m_id;
			++moved_count;
			if (ImGui::GetIO().KeyAlt) {
				u32 old_count = links.size();
					
				for (i32 i = links.size() - 1; i >= 0; --i) {
					const NodeEditorLink& link = links[i];
					if (link.getToNode() == node->m_id) {
						for (NodeEditorLink& rlink : links) {
							if (rlink.getFromNode() == node->m_id && rlink.getFromPin() == link.getToPin()) {
								rlink.from = link.from;
								links.erase(i);
							}
						}
					}
				}
					
				unlink_moved_count += old_count != links.size() ? 1 : 0;
				unlink_moved = node->m_id;
			}
		}
	}

	if (moved_count > 0) {
		if (unlink_moved_count > 1) pushUndo(NO_MERGE_UNDO);
		else if (unlink_moved_count == 1) pushUndo(unlink_moved);
		else if (moved_count > 1) pushUndo(NO_MERGE_UNDO - 1);
		else pushUndo(moved);
	}
		
	i32 hovered_link = -1;
	for (i32 i = 0, c = links.size(); i < c; ++i) {
		NodeEditorLink& link = links[i];
		ImGuiEx::NodeLinkEx(link.from | OUTPUT_FLAG, link.to, link.color, ImGui::GetColorU32(ImGuiCol_TabActive));
		if (ImGuiEx::IsLinkHovered()) {
			if (ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyCtrl) {
				if (ImGuiEx::IsLinkStartHovered()) {
					ImGuiEx::StartNewLink(link.to, true);
				}
				else {
					ImGuiEx::StartNewLink(link.from | OUTPUT_FLAG, false);
				}
				links.erase(i);
				--c;
			}
			if (ImGui::IsMouseDoubleClicked(0)) {
				onLinkDoubleClicked(link, ImGui::GetMousePos() - origin - m_offset);
			}
			else {
				hovered_link = i;
			}
		}
	}

	if (hovered_link >= 0 && ImGui::IsMouseReleased(0) && ImGui::GetIO().KeyAlt) {
		i32 node_idx = nodes.find([this](const NodeEditorNode* node){ return node->m_id == m_dragged_node; });
		if (node_idx >= 0) {
			splitLink(nodes[node_idx], links, hovered_link);
		}
	}

	if (ImGui::IsMouseReleased(0)) m_dragged_node = 0xffFFffFF;

	{
		ImGuiID start_attr, end_attr;
		if (ImGuiEx::GetHalfLink(&start_attr)) {
			m_half_link_start = start_attr;
		}

		if (ImGuiEx::GetNewLink(&start_attr, &end_attr)) {
			ASSERT(start_attr & OUTPUT_FLAG);
			links.eraseItems([&](const NodeEditorLink& link) { return link.to == end_attr; });
			links.push({u32(start_attr) & ~OUTPUT_FLAG, u32(end_attr)});
			
			pushUndo(NO_MERGE_UNDO);
		}
	}

	ImGuiEx::EndNodeEditor();
		
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
		if (ImGui::GetIO().KeyAlt && hovered_link != -1) {
			links.erase(hovered_link);
			pushUndo(NO_MERGE_UNDO);
		}
		else {
			onCanvasClicked(ImGui::GetMousePos() - origin - m_offset, hovered_link);
		}
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
		ImGui::OpenPopup("context_menu");
		m_half_link_start = 0;
	}

	if (ImGui::BeginPopup("context_menu")) {
		const ImVec2 pos = ImGui::GetMousePosOnOpeningCurrentPopup() - origin - m_offset;
		onContextMenu(pos);
		ImGui::EndPopup();
	}		

	m_is_any_item_active = ImGui::IsAnyItemActive();
	m_mouse_pos_canvas = ImGui::GetMousePos() - origin - m_offset;

	m_canvas.end();
}

bool TextFilter::pass(StringView text) const {
	for (u32 i = 0; i < count; ++i) {
		if (*subfilters[i].begin == '-') {
			if (findInsensitive(text, StringView(subfilters[i].begin + 1, subfilters[i].end))) return false;
		}
		else {
			if (!findInsensitive(text, subfilters[i])) return false;
		}
	}
	return true;
}

void TextFilter::build() {
	count = 0;
	StringView tmp;
	tmp.begin = filter;
	tmp.end = filter;
	for (;;) {
		if (*tmp.end == ' ' || *tmp.end == '\0') {
			if (tmp.size() > 0) {
				if (tmp.size() > 1 || *tmp.begin != '-') {
					subfilters[count] = tmp;
					++count;
					if (count == lengthOf(subfilters)) break;
				}
			}
			if (*tmp.end == '\0') break;
			tmp.begin = tmp.end + 1;
			tmp.end = tmp.begin;
		}
		else {
			++tmp.end;
		}
	}
}

bool TextFilter::gui(const char* hint, float width, bool set_keyboard_focus) {
	if (ImGuiEx::Filter(hint, filter, sizeof(filter), width, set_keyboard_focus)) {
		build();
		return true;
	}
	return false;
}

void openCenterStrip(const char* str_id) {
	ImGui::OpenPopup(str_id);
}

bool beginCenterStrip(const char* str_id, u32 lines) {
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(vp->Size.x - style.FramePadding.x * 2, ImGui::GetTextLineHeightWithSpacing() * lines));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
							 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
	return ImGui::BeginPopupModal(str_id, nullptr, flags);
}

void endCenterStrip() {
	ImGui::EndPopup();
}

} // namespace Lumix