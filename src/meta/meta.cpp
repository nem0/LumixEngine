#include "core/defer.h"
#include <float.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#ifdef _WIN32
	#include <Windows.h>
#else
	#include <dirent.h>
	#include <limits.h>
	#include <sys/mman.h>
	#include <sys/stat.h>
	#include <time.h>
#endif
#ifndef _WIN32
static int _itoa_s(int value, char* out, int radix) { return snprintf(out, 32, radix == 10 ? "%d" : "%x", value) < 0; }
static int _ui64toa_s(unsigned long long value, char* out, size_t, int radix) { return snprintf(out, 64, radix == 10 ? "%llu" : "%llx", value) < 0; }
static int strcpy_s(char* dst, size_t size, const char* src) { if (strlen(src) + 1 > size) return 1; strcpy(dst, src); return 0; }
static int strncpy_s(char* dst, size_t size, const char* src, size_t count) { if (count + 1 > size) return 1; memcpy(dst, src, count); dst[count] = 0; return 0; }
#endif
#include "meta.h"

#ifndef MAX_PATH
	#ifdef _WIN32
		#define MAX_PATH 260
	#else
		#define MAX_PATH PATH_MAX
	#endif
#endif

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash/xxhash.h"

// we use crlf in output to avoid unnecessary changes because git converts to crlf 
#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)


struct ArenaAllocator : IAllocator {
	static constexpr size_t CAPACITY = 1024*1024*1024;
	ArenaAllocator() {
#ifdef _WIN32
		mem = VirtualAlloc(nullptr, CAPACITY, MEM_RESERVE, PAGE_READWRITE);
#else
		mem = mmap(nullptr, CAPACITY, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mem == MAP_FAILED) mem = nullptr;
#endif
	}

	~ArenaAllocator() {
#ifdef _WIN32
		VirtualFree(mem, 0, MEM_RELEASE);
#else
		if (mem) munmap(mem, CAPACITY);
#endif
	}
	
	void* allocate(size_t size) override {
		if (allocated + size > comitted) {
			static constexpr size_t PAGE_SIZE = 4096;
			size_t required = allocated + size;
			size_t new_commited = (required + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
			if (new_commited > CAPACITY) return nullptr;
#ifdef _WIN32
			if (!VirtualAlloc((char*)mem + comitted, new_commited - comitted, MEM_COMMIT, PAGE_READWRITE)) return nullptr;
#else
			if (mprotect((char*)mem + comitted, new_commited - comitted, PROT_READ | PROT_WRITE) != 0) return nullptr;
#endif
			comitted = new_commited;
		}

		allocated += size;
		return (char*)mem + allocated - size;
	}

	void deallocate(void* mem) override {}

	void* mem;
	size_t allocated = 0;
	size_t comitted = 0;
};

StringView makeStringView(const char* str) {
	StringView v;
	v.begin = str;
	v.end = str + strlen(str);
	return v;
}

StringView find(StringView haystack, StringView needle) {
	if (needle.size() > haystack.size()) return {};

	const char* search_end = haystack.end - needle.size() + 1;
	const char needle0 = needle[0];

	const char* c = haystack.begin;
	while (c != search_end) {
		if (*c == needle0) {
			const char* n = needle.begin + 1;
			const char* c2 = c + 1;
			while (n != needle.end && c2 != haystack.end) {
				if (*n != *c2) break;
				++n;
				++c2;
			}
			if (n == needle.end) return {c, haystack.end };
		}
		++c;
	}
	return {};
}

StringView find(StringView haystack, const char* needle) {
	return find(haystack, makeStringView(needle));
}

struct StringBuilder {
	StringBuilder(char* buffer, i32 capacity)
		: data(buffer)
		, capacity(capacity)
	{}

	char* data = nullptr;
	i32 capacity = 0;
	i32 length = 0;
	
	void add(const char* v) {
		add(makeStringView(v));
	}

	void add(i32 v) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%d", v);
		add(tmp);
	}

	void add(StringView v) {
		i32 len = v.size();
		if (length + len > capacity) {
			len = capacity - len;
		}
		memcpy(data + length, v.begin, len);
		length += len;
		data[length] = 0;
	}

	operator const char* () { return data; }
	operator StringView () { return {data, data + length}; }
};


template <i32 CAPACITY, typename... Args>
StringView buildString(char (&buffer)[CAPACITY], Args... args) {
	StringBuilder builder(buffer, CAPACITY);
	(builder.add(args), ...);
	return builder;
}

template <typename... Args>
void logInfo(Args... args) {
	char buffer[4096];
	StringBuilder builder(buffer, sizeof(buffer));
	(builder.add(args), ...);
	builder.add("\n");
#ifdef _WIN32
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD written;
	WriteFile(hStdout, buffer, (DWORD)strlen(buffer), &written, NULL);
#else
	fputs(buffer, stdout);
#endif
}

#ifdef _WIN32
struct FileIterator { WIN32_FIND_DATAA ffd; HANDLE handle; bool is_valid; };
#else
struct FileIterator { DIR* handle; bool is_valid; };
#endif

struct FileInfo {
	bool is_directory;
	char name[MAX_PATH];
};

static bool getNextFile(FileIterator& iterator, FileInfo& info) {
	if (!iterator.is_valid) return false;
#ifdef _WIN32
	info.is_directory = (iterator.ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	buildString(info.name, iterator.ffd.cFileName);
	iterator.is_valid = FindNextFile(iterator.handle, &iterator.ffd) != FALSE;
#else
	dirent* entry = readdir(iterator.handle);
	if (!entry) { iterator.is_valid = false; return false; }
	info.is_directory = entry->d_type == DT_DIR;
	buildString(info.name, entry->d_name);
#endif
	return true;
}

static void destroyFileIterator(FileIterator iterator) {
#ifdef _WIN32
	FindClose(iterator.handle);
#else
	if (iterator.handle) closedir(iterator.handle);
#endif
}

static FileIterator createFileIterator(StringView path) {
	FileIterator iter;
#ifdef _WIN32
	char pattern[MAX_PATH];
	buildString(pattern, path, "/*");
	iter.handle = FindFirstFileA(pattern, &iter.ffd);
	iter.is_valid = iter.handle != INVALID_HANDLE_VALUE;
#else
	char directory[PATH_MAX];
	buildString(directory, path);
	iter.handle = opendir(directory);
	iter.is_valid = iter.handle != nullptr;
#endif
	return iter;
}

static bool endsWith(StringView str, const char* suffix) {
	i32 plen = (i32)strlen(suffix);
	if (plen > str.size()) return false;

	const char* a = str.end - plen;
	const char* b = suffix;
	while (*b) {
		if (*a != *b) return false;
		++a;
		++b;
	}
	return true;
}

bool startsWith(StringView str, const char* prefix) {
	const char* c = prefix;
	const char* d = str.begin;
	while (*c && d != str.end && *c == *d) {
		++c;
		++d;
	}
	return !*c;
}

bool startsWith(StringView str, StringView prefix) {
	const char* c = prefix.begin;
	const char* d = str.begin;
	while (c != prefix.end && d != str.end && *c == *d) {
		++c;
		++d;
	}
	return c == prefix.end;
}

bool equal(StringView lhs, const char* rhs) {
	const char* a = lhs.begin;
	const char* b = rhs;
	while (*b && a != lhs.end && *a == *b) {
		++a;
		++b;
	}
	return !*b && a == lhs.end;
}

bool equal(StringView lhs, StringView rhs) {
	const char* a = lhs.begin;
	const char* b = rhs.begin;
	while (b != rhs.end && a != lhs.end && *a == *b) {
		++a;
		++b;
	}
	return b == rhs.end && a == lhs.end;
}

static StringView skipWhitespaces(StringView v) {
	StringView res = v;
	while (res.begin != res.end && isspace(*res.begin)) ++res.begin;
	return res;
}

bool isWordSeparator(char c) {
	return isspace(c) || c == '(' || c == ',' || c == '{' || c == ';' || c == '}' || c == '<';
}

StringView consumeWord(StringView& str) {
	str = skipWhitespaces(str);
	StringView word;
	word.begin = str.begin;
	word.end = word.begin;
	while (word.end != str.end && !isWordSeparator(*word.end)) ++word.end;
	if (word.begin == word.end && word.end < str.end) ++word.end;
	str.begin = word.end;
	str = skipWhitespaces(str);
	return word;
}

StringView peekWord(StringView str) {
	return consumeWord(str);
}

StringView consumeString(StringView& str) {
	str = skipWhitespaces(str);
	if (str.size() < 2) return {};
	if (str[0] != '"') return {};

	StringView result;
	result.begin = str.begin + 1;
	result.end = result.begin;

	while (result.end != str.end && *result.end != '"') ++result.end;
	str.begin = result.end;
	if (str.begin != str.end) ++str.begin; // skip ending "
	str = skipWhitespaces(str);
	return result;
}

char peekChar(StringView str) {
	const char* c = str.begin;
	while (c != str.end && isspace(*c)) ++c;
	if (c != str.end) return *c;
	return 0;
}

StringView consumeType(StringView& str) {
	StringView word = consumeWord(str);
	if (equal(word, "struct")) { // struct S* foo();
		word = consumeWord(str);
	}
	if (equal(word, "const")) {
		StringView word2 = consumeWord(str);
		word.end = word2.end;
	}
	if (peekChar(str) == '<') {
		while (word.end != str.end && *word.end != '>') {
			++word.end;
		}
		if (word.end != str.end) ++word.end;
	}
	str.begin = word.end;

	if (word.end != str.end) {
		char c = peekChar(str);
		if (c == '&' || c == '*') {
			++word.end;
		}
	}
	str.begin = word.end;
	return word;
}

StringView consumeIdentifier(StringView& str) {
	return consumeWord(str);
}

StringView withoutPrefix(StringView str, i32 prefix_len) {
	StringView res = str;
	res.begin += prefix_len;
	return res;
}

StringView withoutSuffix(StringView str, i32 suffix_len) {
	StringView res = str;
	res.end -= suffix_len;
	return res;
}

OutputStream::OutputStream() {
	data = new char[capacity];
}

OutputStream::~OutputStream() {
	delete[] data;
}

void OutputStream::consume(OutputStream& rhs) {
	delete[] data;
	data = rhs.data;
	capacity = rhs.capacity;
	length = rhs.length;
	rhs.data = nullptr;
	rhs.capacity = 0;
	rhs.length = 0;
}

void OutputStream::append(const char* v) {
	append(makeStringView(v));
}

void OutputStream::append(const char v) {
	append(StringView(&v, &v + 1));
}

void OutputStream::append(XXH64_hash_t hash) {
	char cstr[32] = "";
	_ui64toa_s(hash, cstr, sizeof(cstr), 10);
	append(makeStringView(cstr));
}

void OutputStream::append(i32 value) {
	char cstr[32] = "";
	_itoa_s(value, cstr, 10);
	append(makeStringView(cstr));
}

void OutputStream::reserve(i32 size) {
	if (capacity >= size) return;

	capacity = size;
	char* new_data = new char[capacity];
	memcpy(new_data, data, length);
	delete[] data;
	data = new_data;
}

void OutputStream::append(StringView v) {
	if (capacity < length + v.size()) {
		capacity = (length + v.size()) * 2;
		char* new_data = new char[capacity];
		memcpy(new_data, data, length);
		delete[] data;
		data = new_data;
	}
	memcpy(data + length, v.begin, v.size());
	length += v.size();
}

bool readLine(StringView& content, StringView& line) {
	if (content.size() == 0) return false;

	line.begin = content.begin;
	line.end = line.begin;
	
	while (line.end != content.end && *line.end != '\n') {
		++line.end;
	}
	line = skipWhitespaces(line);
	content.begin = line.end;
	if (content.begin != content.end) ++content.begin; // skip \n
	if (line.end > line.begin && *(line.end - 1) == '\r') --line.end;
	return true;
}

void toLabel(StringView in, Span<char> out) {
	char* to = out.begin;
	const char* from = in.begin;
	while (from < in.end && to < out.end) {
		if (*from >= 'A' && *from <= 'Z' && from != in.begin) {
			*to = ' ';
			++to;
			if (to == out.end) break;
		}
		if (from == in.begin) {
			if (*from < 'a' || *from > 'z') {
				*to = *from;
			}
			else {
				*to = *from + ('A' - 'a');
			}
		}
		else {
			if (*from == '_')
				*to = ' ';
			else if (*from >= 'A' && *from <= 'Z')
				*to = *from + ('a' - 'A');
			else
				*to = *from;
		}
		++to;
		++from;
	}
	if (to < out.end) {
		*to = 0;
	}
	else {
		*(to - 1) = 0;
	}
}

void toID(StringView name, Span<char> out) {
	char* dst = out.begin;
	const char* src = name.begin;
	bool prev_lowercase = false;
	while (dst < out.end - 1 && src < name.end) {
		if (*src == ' ') {
			*dst = '_';
		}
		else if (*src >= 'A' && *src <= 'Z') {
			if (src != name.begin && prev_lowercase) {
				*dst = '_';
				++dst;
			}
			if (dst != out.end - 1) *dst = *src | 0x20;
		}
		else {
			prev_lowercase = *src >= 'a' && *src <= 'z';
			*dst = *src;
		}
		++dst;
		++src;
	}
	*dst = 0;
}

struct Parser {
	Parser(IAllocator& allocator)
		: allocator(allocator)
		, modules(allocator)
		, structs(allocator)
		, objects(allocator)
		, enums(allocator)
		, aliases(allocator)
	{}

	bool readLine(StringView& line) {
		if (!::readLine(content, line)) return false;
		++line_idx;
		return true;
	}

	template <typename... Args>
	void logError(Args... args) {
		char line_str[8] = "4";
		_itoa_s(line_idx, line_str, 10);
		logInfo(filename, "(", line_str, "): ", args...); // TODO logInfo?
	}

	bool parseAttributes(StringView def, Attributes& attributes) {
		if (def.size() == 0) return false;

		StringView word = consumeWord(def);
		while (word.size() > 0) {
			if (equal(word, "radians")) {
				attributes.is_radians = true;
			}
			else if (equal(word, "resource_type")) {
				attributes.resource_type = consumeWord(def);
			}
			else if (equal(word, "color")) {
				attributes.is_color = true;
			}
			else if (equal(word, "dynenum")) {
				attributes.dynamic_enum_name = consumeWord(def);
			}
			else if (equal(word, "no_ui")) {
				attributes.no_ui = true;
			}
			else if (equal(word, "min")) {
				attributes.min = consumeWord(def);
			}
			else if (equal(word, "multiline")) {
				attributes.is_multiline = true;
			}
			else if (equal(word, "clamp")) {
				attributes.min = consumeWord(def);
				attributes.clamp_max = consumeWord(def);
			}
			else if (equal(word, "function")) {
				attributes.force_function = true;
			}
			else if (equal(word, "label")) {
				attributes.label = consumeString(def);
			}
			else if (equal(word, "alias")) {
				attributes.alias = consumeIdentifier(def);
			}
			else if (equal(word, "getter")) {
				attributes.property_name = consumeIdentifier(def);
				attributes.force_getter = true;
			}
			else if (equal(word, "setter")) {
				attributes.property_name = consumeIdentifier(def);
				attributes.force_setter = true;
			}
			else {
				logError("Unknown attribute ", word);
			}
			word = consumeWord(def);
		}
		return true;
	}

	bool readFunctionDeclaration(StringView& str) {
		const char* cursor = str.begin;
		i32 depth = 0;
		bool opened = false;
		for (;;) {
			while (cursor != str.end) {
				const char c = *cursor++;
				if (c == '(') {
					opened = true;
					++depth;
				}
				else if (c == ')' && opened) {
					if (--depth == 0) return true;
				}
				else if (c == ';' || c == '{' || c == '}') {
					logError("Incomplete function declaration");
					return false;
				}
			}
			StringView next;
			if (!readLine(next)) {
				logError("Unterminated function argument list");
				return false;
			}
			// Lines share a source buffer; retain their intervening whitespace.
			str.end = next.end;
		}
	}

	bool consumeArgs(StringView& str, StringView& args) {
		str = skipWhitespaces(str);
		args = {};
		if (str.size() == 0 || *str.begin != '(') {
			logError("Expected '(' at start of function argument list");
			return false;
		}
		const char* end = str.begin;
		i32 depth = 0;
		do {
			if (*end == '(') ++depth;
			else if (*end == ')') --depth;
			++end;
		} while (end != str.end && depth > 0);
		if (depth != 0) {
			logError("Unterminated function argument list: expected ')'");
			return false;
		}
		args = {str.begin + 1, end - 1};
		str.begin = end;
		return true;
	}

	Function consumeCPPFunction(StringView& str) {
		Function res;
		if (!readFunctionDeclaration(str)) return res;
		res.return_type = consumeType(str);
		res.name = consumeIdentifier(str);
		if (!consumeArgs(str, res.args)) return {};
		StringView suffix = skipWhitespaces(str);
		res.is_const = suffix.size() >= 5
			&& suffix.begin[0] == 'c' && suffix.begin[1] == 'o' && suffix.begin[2] == 'n'
			&& suffix.begin[3] == 's' && suffix.begin[4] == 't';
		StringView def = find(str, "//@");
		if (def.size() > 2) {
			def.begin += 3;
			parseAttributes(def, res.attributes);
		}
		return res;
	}

	void propertyVariable(StringView line, StringView def) {
		line = skipWhitespaces(line);
		StringView type = consumeType(line);
		StringView name = consumeIdentifier(line);
		if (name.end != name.begin && *(name.end - 1) == ';') --name.end; // trim ';'
		
		StringView label = name;
		Attributes attributes;
		parseAttributes(def, attributes);
		
		Property& prop = getProperty(name);
		prop.is_var = true;
		prop.type = type;
		merge(prop.attributes, attributes);
	}

	void parseComponentStruct(StringView def) {
		StringView icon;
		StringView name;
		StringView word = consumeWord(def);
		StringView label;
		StringView id;
		while (word.size() > 0) {
			if (equal(word, "icon")) {
				icon = consumeWord(def);
			}
			else if (equal(word, "name")) {
				name = consumeWord(def);
			}
			else if (equal(word, "label")) {
				label = consumeString(def);
			}
			else if (equal(word, "id")) {
				id = consumeIdentifier(def);
			}
			else {
				logError("Unexpected ", word);
			}
			word = consumeWord(def);
		}
		
		StringView line;
		if (!readLine(line) || !equal(consumeWord(line), "struct")) {
			logError("Expected 'struct'");
			return;
		}
		StringView struct_name = consumeWord(line);
		if (struct_name.size() == 0) {
			logError("Expected struct name");
			return;
		}
		
		if (name.size() == 0) name = struct_name;
		
		if (id.size() > 0) {
			beginComponent(name, struct_name, id, label, icon);
		}
		else {
			char tmp[256];
			toID(struct_name, Span(tmp, tmp + 255));
			const size_t id_strlen = strlen(tmp);
			char* id_str = (char*)allocator.allocate(id_strlen + 1);
			strcpy_s(id_str, id_strlen + 1, tmp);

			beginComponent(name, struct_name, makeStringView(id_str), label, icon);
		}
		defer { current_component = nullptr; };
		
		while (readLine(line)) {
			StringView def = find(line, "//@");
			if (def.size() == 0) continue;
			
			def.begin += 3;
			StringView word = consumeWord(def);
			if (equal(word, "property")) {
				propertyVariable(line, def);
			}
			else if (equal(word, "end")) {
				return;
			}
			else {
				logError("Unexpected \"", word, "\"");
			}
		}		
		logError("'//@ end' not found while parsing component ", struct_name);
	}

	bool consumePrefix(StringView& str, const char* prefix) {
		const char* a = str.begin;
		const char* b = prefix;
		while (a < str.end && *b && *a == *b) {
			++a;
			++b;
		}
		if (*b) return false;
		str.begin = a;
		return true;
	}

	bool consumePrefix(StringView& str, StringView prefix) {
		const char* a = str.begin;
		const char* b = prefix.begin;
		while (a < str.end && b < prefix.end && *a == *b) {
			++a;
			++b;
		}
		if (b < prefix.end) return false;
		str.begin = a;
		return true;
	}

	void parseArray(StringView component_name, StringView array_name, StringView array_id) {
		StringView line;

		ArrayProperty& a = current_component->arrays.emplace(allocator);
		a.id = array_id;
		a.name = array_name;
		
		while (readLine(line)) {
			StringView word = consumeWord(line);
			if (equal(word, "//@")) {
				line = skipWhitespaces(line);
				word = consumeWord(line);
				if (equal(word, "end")) {
					return;
				}
				else {
					logError("Unexpected //@", word);
				}
			}
			else if (equal(word, "virtual")) {
				Function fn = consumeCPPFunction(line);
				if (fn.name.size() == 0) continue;

				StringView property_name = fn.name;
				if (consumePrefix(property_name, "get") || consumePrefix(property_name, "is")) {
					if (!consumePrefix(property_name, array_name)) {
						logError("Expected ", array_name);
						continue;
					}
					if (!equal(property_name, "Count")) {
						Property& prop = getChild(a, property_name);
						merge(prop.attributes, fn.attributes);
						prop.getter_name = fn.name;
						prop.getter_args = fn.args;
						prop.type = fn.return_type;
					}
				}
				else if (consumePrefix(property_name, "set")) {
					if (!consumePrefix(property_name, array_name)) {
						logError("Expected ", array_name);
						continue;
					}
					Property& prop = getChild(a, property_name);
					merge(prop.attributes, fn.attributes);
					prop.setter_name = fn.name;
					prop.setter_args = fn.args;
				}
				else if (consumePrefix(property_name, "enable")) {
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
						continue;
					}
					Property& prop = getChild(a, makeStringView("Enabled"));
					merge(prop.attributes, fn.attributes);
					prop.setter_name = fn.name;
					prop.setter_args = fn.args;
				}
			}
		}
		logError("'//@ end' not found while parsing ", component_name, ".", array_name);
	}

	void parseComponent(StringView component_name, StringView def) {
		StringView id;
		StringView label;
		StringView icon;

		StringView word = consumeWord(def);
		while (word.size() > 0) {
			if (equal(word, "icon")) {
				icon = consumeWord(def);
			}
			else if (equal(word, "id")) {
				id = consumeIdentifier(def);
			}
			else if (equal(word, "label")) {
				label = consumeString(def);
			}
			else {
				logError("Unexpected ", word);
			}
			word = consumeWord(def);
		}

		char tmp[256];
		if (label.size() == 0) {
			toLabel(component_name, Span(tmp, tmp + sizeof(tmp)));
			const size_t len = strlen(tmp) + 1;
			char* l = (char*)allocator.allocate(len);
			strcpy_s(l, len, tmp);
			label = makeStringView(l);
		}
		if (id.size() == 0) {
			toID(component_name, Span(tmp, tmp + sizeof(tmp)));

			const size_t len = strlen(tmp) + 1;
			char* l = (char*)allocator.allocate(len);
			strcpy_s(l, len, tmp);
			id = makeStringView(l);
		}

		beginComponent(component_name, {}, id, label, icon);
		defer { current_component = nullptr; };

		StringView line;
		while (readLine(line)) {
			StringView word = consumeWord(line);
			if (equal(word, "//@")) {
				line = skipWhitespaces(line);
				word = consumeWord(line);
				if (equal(word, "end")) {
					return;
				}
				else if (equal(word, "array")) {
					StringView array_name = consumeIdentifier(line);
					StringView array_id = consumeIdentifier(line);
					parseArray(component_name, array_name, array_id);
				}
				else {
					logError("Unexpected \"", word, "\"");
				}
			}
			else if (equal(word, "virtual")) {
				Function fn = consumeCPPFunction(line);
				if (fn.name.size() == 0) continue;

				StringView property_name = fn.name;
				if (fn.attributes.force_function) {
					current_component->functions.emplace(fn);
				}
				else if (fn.attributes.force_setter) {
					setter(fn.name, fn.attributes.property_name, fn.args, fn.attributes);
				}
				else if (fn.attributes.force_getter) {
					getter(fn.return_type, fn.name, fn.attributes.property_name, fn.args, fn.attributes);
				}
				else if (consumePrefix(property_name, "set")) {
					if (!consumePrefix(property_name, component_name)) {
						logError("Expected ", component_name);
						continue;
					}
					setter(fn.name, property_name, fn.args, fn.attributes);
				}
				else if (consumePrefix(property_name, "get") || consumePrefix(property_name, "is")) {
					if (!consumePrefix(property_name, component_name)) {
						logError("Expected ", component_name);
						return;
					}
					getter(fn.return_type, fn.name, property_name, fn.args, fn.attributes);
				}
				else if (startsWith(property_name, "enable")) {
					setter(fn.name, makeStringView("Enabled"), fn.args, fn.attributes);
				}
				else {
					current_component->functions.emplace(fn);
				}
			}
		}		
		logError("'//@ end' not found while parsing component ", component_name);
	}

	void parseEvents() {
		StringView line;
		while (readLine(line)) {
			StringView word = consumeWord(line);
			if (equal(word, "virtual")) {
				StringView type = consumeType(line);
				StringView method_name = consumeIdentifier(line);
				current_module->events.emplace() = method_name;
			}
			else if (equal(word, "//@")) {
				word = consumeWord(line);
				if (equal(word, "end")) return;

				logError("Unexpected ", word);
			}
		}
	}

	void parseFunctions() {
		StringView line;
		while (readLine(line)) {
			StringView word = consumeWord(line);
			if (equal(word, "virtual")) {
				Function fn = consumeCPPFunction(line);
				if (fn.name.size() == 0) continue;
				current_module->functions.emplace(fn);
			}
			else if (equal(word, "//@")) {
				word = consumeWord(line);
				if (equal(word, "end")) return;

				logError("Unexpected ", word);
			}
		}
	}
	
	void parseEnum(StringView def, ExpArray<Enum>& enums) {
		StringView word = consumeWord(def);
		StringView full;
		while (word.size() > 0) {
			if (equal(word, "full")) {
				full = consumeIdentifier(def);
			}
			else {
				logError("Unknown ", word);
			}
			word = consumeWord(def);
		}
		
		StringView line;
		if (!readLine(line)) return;
		StringView word0 = consumeWord(line);
		if (!equal(word0, "enum")) {
			logError("Expected enum");
			return;
		}
		StringView enum_name = consumeWord(line);
		if (equal(enum_name, "class")) enum_name = consumeWord(line);

		Enum& e = enums.emplace(allocator);
		e.filename = (char*)allocator.allocate(filename.size() + 1);
		strncpy_s(e.filename, filename.size() + 1, filename.begin, filename.size());
		e.full = full.size() > 0 ? full : enum_name;
		e.name = enum_name;
		e.underlying_type = makeStringView("int");
		if (equal(consumeWord(line), ":")) {
			line = skipWhitespaces(line);
			e.underlying_type = line;
			while (line.size() > 0 && line[0] != '{' && line[0] != ';') ++line.begin;
			e.underlying_type.end = line.begin;
			while (e.underlying_type.size() > 0 && (e.underlying_type[e.underlying_type.size() - 1] == ' ' || e.underlying_type[e.underlying_type.size() - 1] == '\t')) --e.underlying_type.end;
		}
		last_enumerator_value = ~u64(0);

		if (find(line, ";").size() > 0) {
			// one line enum or forward decl - e.g. enum Handle : i32;
			return;
		}

		for (;;) {
			if (!readLine(line)) {
				logError("End of enum not found");
				return;
			}
			StringView enumerator_name = consumeWord(line);
			if (equal(enumerator_name, "}")) break;
			if (enumerator_name.size() == 0) continue;
		
			StringView enumerator_value = consumeWord(line);
			if (equal(enumerator_value, "=")) {
				enumerator_value = consumeWord(line);
			}
			else enumerator_value = {};

			Enumerator& e = enums.last().values.emplace();
			e.name = enumerator_name;
			if (enumerator_value.size() > 0) {
				char tmp[64];
				buildString(tmp, enumerator_value);
				// Handle character literals like 'A' and escape sequences, otherwise parse numeric (dec/hex)
				if (tmp[0] == '\'' ) {
					int val = 0;
					if (tmp[1] == '\\') {
						// escaped sequence
						char esc = tmp[2];
						switch (esc) {
							case 'n': val = '\n'; break;
							case 'r': val = '\r'; break;
							case 't': val = '\t'; break;
							case '\\': val = '\\'; break;
							case '\'': val = '\''; break;
							case '"': val = '"'; break;
							case '0': val = '\0'; break;
							case 'x': {
								// parse hex after \x
								char* endptr = nullptr;
								val = (int)strtol(tmp + 3, &endptr, 16);
								break;
							}
							default: val = (int)esc; break;
						}
					}
					else {
						val = (int)tmp[1];
					}
					e.value = val;
					last_enumerator_value = e.value;
				}
				else {
					e.value = strtoull(tmp, nullptr, 0);
					last_enumerator_value = e.value;
				}
			}
			else {
				++last_enumerator_value;
				e.value = last_enumerator_value;
			}

		}
	}

	void parseModule(StringView module_name, StringView id, StringView label) {
		Module& m = modules.emplace(allocator);
		m.filename = (char*)allocator.allocate(filename.size() + 1);
		strncpy_s(m.filename, filename.size() + 1, filename.begin, filename.size());
		current_module = &m;
		m.id = id;
		m.label = label;
		m.name = module_name;

		StringView line;
		while (readLine(line)) {
			if (!consumePrefix(line, "//@")) continue;
			
			line = skipWhitespaces(line);
			StringView word = consumeWord(line);
			if (equal(word, "functions")) {
				parseFunctions();
			}
			else if (equal(word, "enum")) {
				parseEnum(line, current_module->enums);
			}
			else if (equal(word, "alias")) {
				parseAlias();
			}
			else if (equal(word, "include")) {
				StringView path = consumeString(line);
				m.includes.emplace(path);
			}
			else if (equal(word, "events")) {
				parseEvents();
			}
			else if (equal(word, "component")) {
				StringView cmp_name = consumeWord(line);
				parseComponent(cmp_name, line);
			}
			else if (equal(word, "component_struct")) {
				parseComponentStruct(line);
			}
			else if (equal(word, "end")) {
				return;
			}
			else {
				logError("Unexpected \"", word, "\"");
			}
		}
	}

	static StringView parseStructName(StringView line, StringView* base = nullptr) {
    	StringView word = consumeWord(line);
    	if (!equal(word, "struct")) return {};

		auto parse_base = [](StringView rest) {
			if (peekChar(rest) == ':') consumeWord(rest);
			return consumeWord(rest);
		};
		
		StringView last_ident;
		for (;;) {
			StringView w = consumeWord(line);
			if (w.size() == 0) return last_ident;
			if (equal(w, "final")) {
				if (base && peekChar(line) == ':') *base = parse_base(line);
				return last_ident;
			}

			if (w.size() > 0 && w[w.size() - 1] == ':') {
				--w.end;
				if (base) *base = parse_base(line);
				return w;
			}
			
			// stop on inheritance or block start
			char c = peekChar(line);
			if (c == ':' || c == '{' || c == ';') {
				if (base && c == ':') *base = parse_base(line);
				return w;
			}

			last_ident = w;
		}
	}

	void parseObject(StringView def) {
		StringView word = consumeWord(def);
		StringView full;
		while (word.size() > 0) {
			if (equal(word, "full")) {
				full = consumeIdentifier(def);
			}
			else {
				logError("Unknown ", word);
			}
			word = consumeWord(def);
		}

		StringView line;
		if (!readLine(line)) {
			logError("Expected struct");
			return;
		}
		StringView base;
		StringView struct_name = parseStructName(line, &base);
		if (struct_name.size() == 0) {
			logError("Expected struct");
			return;
		}

		Object& o = objects.emplace(allocator);
		o.full = full.size() > 0 ? full : struct_name;
		o.name = struct_name;
		o.base = base;
		o.filename = (char*)allocator.allocate(filename.size() + 1);
		strncpy_s(o.filename, filename.size() + 1, filename.begin, filename.size());

		while (readLine(line)) {
			StringView word = consumeWord(line);
			if (equal(word, "//@")) {
				word = consumeWord(line);
				if (equal(word, "end")) {
					break;
				}
				else if (equal(word, "function")) {
					if (!readLine(line)) {
						logError("Expected new line with function");
						continue;
					}
					word = peekWord(line);
					if (equal(word, "virtual")) {
						consumeWord(line);
					}
					Function func = consumeCPPFunction(line);
					if (func.name.size() != 0) o.functions.emplace(func);
				}
				else {
					logError("Unexpected ", word);
				}
			}
		}
	}

	
	void parseStruct(StringView def) {
		StringView word = consumeWord(def);
		StringView full;
		while (word.size() > 0) {
			if (equal(word, "full")) {
				full = consumeIdentifier(def);
			}
			else {
				logError("Unknown ", word);
			}
			word = consumeWord(def);
		}

		StringView line;
		if (!readLine(line)) {
			logError("Expected struct");
			return;
		}
		StringView name = parseStructName(line);
		if (name.size() == 0) {
			logError("Failed to parse struct name");
			return;
		}
		Struct& s = structs.emplace(allocator);
		s.filename = (char*)allocator.allocate(filename.size() + 1);
		strncpy_s(s.filename, filename.size() + 1, filename.begin, filename.size());
		s.name = name;
		s.full = full.size() > 0 ? full : name;

		while (readLine(line)) {
			line = skipWhitespaces(line);
			if (line.size() == 0) continue;
			
			if (peekChar(line) == '}') break;

			if (equal(peekWord(line), "static")) {
				continue; // ignore static stuff
			}

			StringView type = consumeType(line);
			if (equal(type, "using")) {
				continue;
			}

			StringView var_name = consumeIdentifier(line);
			if (equal(var_name, "(")) {
				continue; // this is a constructor, ignore
			}

			StringView after_name = consumeWord(line);

			if (equal(after_name, "(")) {
				continue; // this is a function, ignore
			}

			StructVar& v = s.vars.emplace();
			v.type = type;
			v.name = var_name;
		}
	}

	void parseAlias() {
		StringView line;
		if (!readLine(line) || !equal(consumeWord(line), "using")) {
			logError("Expected using Name = Type; after //@alias");
			return;
		}
		const StringView name = consumeIdentifier(line);
		if (name.size() == 0 || !equal(consumeWord(line), "=")) {
			logError("Expected using Name = Type; after //@alias");
			return;
		}
		const StringView type = consumeType(line);
		if (type.size() == 0 || !equal(consumeWord(line), ";")) {
			logError("Expected a simple type alias");
			return;
		}
		TypeAlias& alias = aliases.emplace();
		alias.filename = (char*)allocator.allocate(filename.size() + 1);
		strncpy_s(alias.filename, filename.size() + 1, filename.begin, filename.size());
		alias.name = name;
		alias.type = type;
	}

	void parse() {
		line_idx = 0;
		StringView line;
		while (readLine(line)) {
			if (!startsWith(line, "//@")) continue;
			line.begin += 3;
			line = skipWhitespaces(line);
			StringView word = consumeWord(line);
			if (equal(word, "module")) {
				StringView module_name = consumeWord(line);
				StringView id = consumeWord(line);
				StringView label = consumeString(line);
				parseModule(module_name, id, label);
			}
			else if (equal(word, "enum")) {
				parseEnum(line, enums);
			}
			else if (equal(word, "struct")) {
				parseStruct(line);
			}
			else if (equal(word, "object")) {
				parseObject(line);
			}
			else if (equal(word, "alias")) {
				parseAlias();
			}
			else {
				logError("Unexpected \"", word, "\"");
			}
		}		
	}

	void beginFile(StringView name) {
		filename = name;
	}

	void beginComponent(StringView name, StringView struct_name, StringView id, StringView label, StringView icon) {
		for (Component& cmp : current_module->components) {
			if (equal(cmp.id, id)) {
				current_component = &cmp;
				return;
			}
		}

		Component& c = current_module->components.emplace(allocator);
		c.id = id;
		c.name = name;
		c.struct_name = struct_name;
		c.icon = icon;
		c.label = label;
		current_component = &c;
	}

	void merge(Attributes& dst, const Attributes& src) {
		if (src.label.size() > 0) dst.label = src.label;
		if (src.min.size() > 0) dst.min = src.min;
		if (src.clamp_max.size() > 0) dst.clamp_max = src.clamp_max;
		if (src.resource_type.size() > 0) dst.resource_type = src.resource_type;
		if (src.property_name.size() > 0) dst.property_name = src.property_name;
		if (src.dynamic_enum_name.size() > 0) dst.dynamic_enum_name = src.dynamic_enum_name;
		dst.no_ui = dst.no_ui || src.no_ui;
		dst.is_radians = dst.is_radians || src.is_radians;
		dst.is_color = dst.is_color || src.is_color;
		dst.force_function = dst.force_function || src.force_function;
		dst.force_getter = dst.force_getter || src.force_getter;
		dst.force_setter = dst.force_setter || src.force_setter;
		dst.is_multiline = dst.is_multiline || src.is_multiline;
	}

	void setter(StringView method_name, StringView property_name, StringView args, const Attributes& attributes) {
		Property& prop = getProperty(property_name);
		merge(prop.attributes, attributes);
		prop.setter_name = method_name;
		prop.setter_args = args;
	}

	void getter(StringView return_type, StringView method_name, StringView property_name, StringView args, const Attributes& attributes) {
		Property& prop = getProperty(property_name);
		merge(prop.attributes, attributes);
		prop.getter_name = method_name;
		prop.getter_args = args;
		prop.type = return_type;
	}
	
	Property& getProperty(StringView name) {
		for (Property& p : current_component->properties) {
			if (equal(p.name, name)) return p;
		}

		Property& p = current_component->properties.emplace();
		p.name = name;
		return p;
	}

	Property& getChild(ArrayProperty& array, StringView name) {
		for (Property& p : array.children) {
			if (equal(p.name, name)) return p;
		}

		Property& p = array.children.emplace();
		p.name = name;
		return p;
	}

	IAllocator& allocator;
	StringView filename;
	u64 last_enumerator_value = ~u64(0);
	Component* current_component = nullptr;
	Module* current_module = nullptr;
	ExpArray<Module> modules;
	ExpArray<Struct> structs;
	ExpArray<Object> objects;
	ExpArray<Enum> enums;
	ExpArray<TypeAlias> aliases;
	StringView content;
	i32 line_idx = 0;
};

ArenaAllocator allocator;
Parser parser(allocator);
i32 num_parsed_files = 0;
i32 num_parsed_bytes = 0;

void parseFile(StringView path, StringView filename) {
	char full[MAX_PATH];
	buildString(full, path, "/", filename);

#ifdef _WIN32
	HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;
	defer { CloseHandle(h); };
	DWORD size = GetFileSize(h, nullptr);
	if (size == INVALID_FILE_SIZE) return;
	char* data = (char*)allocator.allocate(size + 1);
	DWORD read = 0;
	if (!ReadFile(h, data, size, &read, nullptr)) return;
#else
	FILE* h = fopen(full, "rb");
	if (!h) return;
	defer { fclose(h); };
	fseek(h, 0, SEEK_END);
	size_t size = ftell(h);
	fseek(h, 0, SEEK_SET);
	char* data = (char*)allocator.allocate(size + 1);
	size_t read = fread(data, 1, size, h);
	if (read != size) return;
#endif
	data[read] = 0;
	++num_parsed_files;
	num_parsed_bytes += (i32)read;

	parser.beginFile(makeStringView(full));
	parser.content.begin = data;
	parser.content.end = data + size;
	parser.parse();
}

void scan(StringView path) {
	FileIterator iter = createFileIterator(path);
	FileInfo fi;
	while (getNextFile(iter, fi)) {
		StringView name = makeStringView(fi.name);
		if (startsWith(name, ".")) continue;
		
		if (fi.is_directory) {
			char tmp[MAX_PATH];
			StringView full = buildString(tmp, path, "/", name);
			scan(full);
		}
		else {
			if (endsWith(name, ".cpp") || endsWith(name, ".h")) {
				parseFile(path, name);
			}
		}
	}
	destroyFileIterator(iter);
}

void serializeAttributes(OutputStream& out, const Attributes& attributes) {
	if (attributes.is_radians)					L("\t\t\t.radiansAttribute()");
	if (attributes.is_multiline)				L("\t\t\t.multilineAttribute()");
	if (attributes.resource_type.size() > 0)	L("\t\t\t.resourceAttribute(", attributes.resource_type, ")");
	if (attributes.is_color)					L("\t\t\t.colorAttribute()");
	if (attributes.no_ui)						L("\t\t\t.noUIAttribute()");
	if (attributes.min.size() > 0)				L("\t\t\t.minAttribute(", attributes.min, ")");
	if (attributes.clamp_max.size() > 0)		L("\t\t\t.clampAttribute(", attributes.min, ", ", attributes.clamp_max, ")");
}

StringView withoutNamespace(StringView ident) {
	StringView res = ident;
	res.begin = ident.end;
	while (res.begin != ident.begin && *res.begin != ':') --res.begin;
	if (*res.begin == ':') ++res.begin;
	return res;
}

static Enum* getEnum(Module& m, StringView name) {
	for (Enum& e : parser.enums) {
		if (equal(e.name, name)) return &e;
		if (equal(e.full, name)) return &e;
	}
	for (Enum& e : m.enums) {
		if (equal(e.name, name)) return &e;
		if (equal(e.full, name)) return &e;
	}
	return nullptr;
}

bool consumeArg(StringView& line, Arg& out) {
	out = Arg();
	line = skipWhitespaces(line);
	if (line.size() == 0) return false;
	StringView word = consumeWord(line);
	if (equal(word, ",")) word = consumeWord(line);
	if (equal(word, "const")) {
		word = consumeWord(line);
		out.is_const = true;
	}
	if (equal(word, "struct")) word = consumeWord(line);
	if (endsWith(word, "&")) {
		out.is_ref = true;
		word.end -= 1;
	}
	if (endsWith(word, "*")) {
		out.is_ptr = true;
		word.end -= 1;
	}
	out.type = word;
	word = consumeWord(line);
	out.name = word;
	return true;
}

void formatCPP(OutputStream& out) {
	OutputStream formatted;
	StringView raw = { out.data, out.data + out.length };
	StringView line;
	const char* tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	i32 indent = 0;
	while (readLine(raw, line)) {
		i32 prev_indent = indent;
		for (const char* c = line.begin; c < line.end; ++c) {
			if (*c == '{') ++indent;
			else if (*c == '}') --indent;
			assert(indent >= 0);
		}
		if (indent > 0) formatted.add(StringView(tabs, tabs + (indent < prev_indent ? indent : prev_indent)));
		formatted.add(line, OUT_ENDL);
	}
	out.consume(formatted);
}

bool isBlob(const Property& p) {
	return equal(p.type, "void");
}

void serializeComponentTypes(Parser& parser) {
	OutputStream out;
	L("// generated by meta.cpp");
	L("#include \"engine/lumix.h\"");
	L("namespace Lumix::types {");
	for (Module& module : parser.modules) {
		for (Component& component : module.components) {
			L("LUMIX_ENGINE_API extern const ComponentType ", component.id, ";");
		}
	}
	L("}");
	writeFile("src/engine/component_types.h", out);

	out.length = 0;
	L("// generated by meta.cpp");
	L("#include \"engine/lumix.h\"");
	L("#include \"core/string.h\"");
	L("namespace Lumix::types {");
	for (Module& module : parser.modules) {
		for (Component& component : module.components) {
			L("extern const ComponentType ", component.id, " = reflection::getComponentType(\"", component.id, "\");");
		}
	}
	L("}");
	writeFile("src/engine/component_types.cpp", out);

}

void serializeReflection(OutputStream& out, Module& m) {
	L("// Generated by meta.cpp" OUT_ENDL);
	for (Enum& e : m.enums) {
		L("struct ", e.name, "Enum : reflection::EnumAttribute {");
		L("\tu32 count(ComponentUID cmp) const override { return ",e.values.size,"; }");
		L("\tconst char* name(ComponentUID cmp, u32 idx) const override {");
		L("\t\tswitch((",e.name,")idx) {");
		for (Enumerator& v : e.values) {
			char tmp[256];
			buildString(tmp, v.name);
			for (char& c : tmp) {
				if (!c) break;
				if (c == '_') c = ' ';
				if (c >= 'A' && c <= 'Z' && &c != tmp) {
					c |= 0x20;
				}
			}
			L("\t\t\tcase ",e.name,"::",v.name,": return \"",tmp,"\";");
		}
		L("\t\t}");
		L("\t\tASSERT(false);");
		L("\t\treturn \"N/A\";");
		L("\t}");
		L("};" OUT_ENDL);
	}

	L("reflection::build_module(\"", m.id, "\")");
	for (StringView e : m.events) {
		L("\t.event<&",m.name,"::",e,">(\"",e,"\")");
	}
	for (Function& fn : m.functions) {
		StringView name = fn.name;
		if (fn.attributes.alias.size() > 0) name = fn.attributes.alias;
		L("\t.function<(", fn.return_type, " (", m.name, "::*)(", fn.args, ")", fn.is_const ? " const" : "", ")&", m.name, "::", fn.name ,">(\"", name, "\")");
	}

	for (Component& cmp : m.components) {
		auto def_property = [&](const Property& prop) {
			if (prop.is_var) {
				out.add("\t\t.var_prop<&", m.name, "::get", cmp.name, ", &", cmp.struct_name, "::", prop.name, ">(\"");
				char label[256];
				if (prop.attributes.label.size() > 0) {
					L(prop.attributes.label, "\")");
				}
				else {
					toLabel(prop.name, Span(label, label + sizeof(label)));
					L(label, "\")");
				}
				serializeAttributes(out, prop.attributes);
				return;
			}

			if (prop.getter_name.size() > 0) {
				if (isBlob(prop)) {
					out.add("\t\t.blob_property<&", m.name, "::", prop.getter_name);
				}
				else {
					out.add("\t\t.prop<&", m.name, "::", prop.getter_name);
				}

				if (prop.setter_name.size() > 0) {
					out.add(", &", m.name, "::", prop.setter_name);
				}

				if (prop.attributes.label.size() > 0) {
					L(">(\"", prop.attributes.label, "\")");
				}
				else {
					char label[256];
					toLabel(prop.name, Span(label, label + sizeof(label)));
					L(">(\"", label, "\")");
				}

				serializeAttributes(out, prop.attributes);
				bool is_enum = getEnum(m, prop.type) || prop.attributes.dynamic_enum_name.size() > 0;
				if (is_enum) {
					// TODO withoutNamespace?
					StringView enum_name = prop.attributes.dynamic_enum_name.size() > 0 ? prop.attributes.dynamic_enum_name : withoutNamespace(prop.type);
					L("\t\t\t.attribute<",enum_name,"Enum>()");
				}
				return;
			}
			
			// if there's only a setter without a getter, we treat it as a function rather than a write-only property
			if (prop.setter_name.size() > 0) {
				L("\t\t.function<&", m.name, "::",  prop.setter_name, ">(\"set", prop.name, "\")");
			}	
		};

		out.add("\t.cmp<&", m.name, "::create", cmp.name, ", &", m.name, "::destroy", cmp.name, ">(\"", cmp.id, "\", \"", m.label, " / ");
		if (cmp.label.size() > 0) {
			L(cmp.label, "\")");
		}
		else {
			char tmp[256];
			toLabel(cmp.name, Span(tmp, tmp + sizeof(tmp)));
			L(tmp, "\")");
		}
		
		if (cmp.icon.size() > 0) L("\t\t.icon(", cmp.icon, ")");

		for (Function& fn : cmp.functions) {
			StringView name = fn.attributes.alias.size() > 0 ? fn.attributes.alias : fn.name;
			L("\t\t.function<(", fn.return_type, " (", m.name, "::*)(", fn.args, ")", fn.is_const ? " const" : "", ")&", m.name, "::", fn.name, ">(\"", name, "\")");
		}

		for (Property& prop : cmp.properties) {
			def_property(prop);
		}

		for (ArrayProperty& array : cmp.arrays) {
			L("\t\t.begin_array<&", m.name, "::get", array.name, "Count, &", m.name, "::add", array.name, ", &", m.name, "::remove", array.name, ">(\"", array.id ,"\")");
			for (Property& prop : array.children) {
				def_property(prop);
			}
			L("\t\t.end_array()");
		}
	}
	L(";" OUT_ENDL);
}

void writeFile(const char* out_path, OutputStream& stream) {
	char directory[MAX_PATH];
	strncpy_s(directory, sizeof(directory), out_path, strlen(out_path));
	char* slash = strrchr(directory, '/');
	if (!slash) slash = strrchr(directory, '\\');
	if (slash) {
		*slash = 0;
		#ifdef _WIN32
				for (char* p = directory; *p; ++p) {
					if (*p != '/' && *p != '\\') continue;
					const char separator = *p;
					*p = 0;
					CreateDirectoryA(directory, nullptr);
					*p = separator;
				}
				CreateDirectoryA(directory, nullptr);
		#else
				mkdir(directory, 0777);
		#endif
	}
#ifdef _WIN32
	HANDLE h_existing = CreateFileA(out_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h_existing != INVALID_HANDLE_VALUE) {
		defer { CloseHandle(h_existing); };
		DWORD existing_size = GetFileSize(h_existing, nullptr);
		if (existing_size == (DWORD)stream.length) {
			char* existing_data = new char[existing_size];
			defer { delete[] existing_data; };
			DWORD read_bytes = 0;
			if (ReadFile(h_existing, existing_data, existing_size, &read_bytes, nullptr) && read_bytes == existing_size && memcmp(existing_data, stream.data, existing_size) == 0) return;
		}
	}
	HANDLE hout = CreateFileA(out_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hout != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(hout, stream.data, (DWORD)stream.length, &written, nullptr);
		CloseHandle(hout);
	}
#else
	FILE* existing = fopen(out_path, "rb");
	if (existing) {
		fseek(existing, 0, SEEK_END);
		size_t size = ftell(existing);
		fseek(existing, 0, SEEK_SET);
		if (size == stream.length) {
			char* data = new char[size];
			const bool same = fread(data, 1, size, existing) == size && memcmp(data, stream.data, size) == 0;
			delete[] data;
			fclose(existing);
			if (same) return;
		}
		else fclose(existing);
	}
	FILE* output = fopen(out_path, "wb");
	if (output) { fwrite(stream.data, 1, stream.length, output); fclose(output); }
#endif
}

int main() {
#ifdef _WIN32
	LARGE_INTEGER start, stop, freq;
	QueryPerformanceCounter(&start);
#else
	timespec start, stop;
	clock_gettime(CLOCK_MONOTONIC, &start);
#endif

	scan(makeStringView("src"));
	scan(makeStringView("plugins"));

	OutputStream stream;
	for (Module& m : parser.modules) {
		char out_path[MAX_PATH];
		const char* b = m.filename;
		const char* e = m.filename + strlen(m.filename);
		const char* dot = e;
		while (dot != b) {
			--dot;
			if (*dot == '.') break;
		}
		if (dot == b || *dot != '.') dot = e; // no extension
		i32 stem_len = i32(dot - b);
		if (stem_len > MAX_PATH - 1) stem_len = MAX_PATH - 1;
		char stem[MAX_PATH];
		memcpy(stem, b, stem_len);
		stem[stem_len] = 0;
		buildString(out_path, stem, ".gen.h");

		stream.length = 0;
		serializeReflection(stream, m);
		writeFile(out_path, stream);
	}
	serializeComponentTypes(parser);

	MetaData metadata = {
		parser.modules,
		parser.structs,
		parser.objects,
		parser.enums,
		parser.aliases
	};

	for (MetaPluginRegister* r = MetaPluginRegister::first; r; r = r->next) {
		(r->fn)(metadata);
	}

#ifdef _WIN32
	QueryPerformanceCounter(&stop);
	QueryPerformanceFrequency(&freq);
	i32 duration = i32(float((stop.QuadPart - start.QuadPart) / double(freq.QuadPart)) * 1000);
#else
	clock_gettime(CLOCK_MONOTONIC, &stop);
	i32 duration = i32((stop.tv_sec - start.tv_sec) * 1000 + (stop.tv_nsec - start.tv_nsec) / 1000000);
#endif
	logInfo("Meta: Processed ",num_parsed_bytes/1024," KB in ",num_parsed_files," files in ", duration, " ms");
	return 0;
}
