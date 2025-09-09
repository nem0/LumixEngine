#include "core/defer.h"
#include <float.h>
#include <Windows.h>
#include <assert.h>

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash/xxhash.h"

// we use crlf in output to avoid unnecessary changes because git converts to crlf 
#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

using i32 = int;

struct NewPlaceholder {};
inline void* operator new(size_t, NewPlaceholder, void* where) { return where; }
inline void operator delete(void*, NewPlaceholder,  void*) { } 

struct IAllocator {
	virtual void* allocate(size_t size) = 0;
	virtual void deallocate(void* mem) = 0;
};

template <typename T>
struct ExpArray {
	ExpArray(IAllocator& allocator) : allocator(allocator) {}
	
	template <typename... Args>
	T& emplace(Args&&... args) {
		if (capacity == size) grow();
		unsigned long m = (size + 4) >> 2;
		unsigned long chunk = 0;
		_BitScanReverse(&chunk, (unsigned long)m);
		i32 chunk_elems = (i32(4) << chunk);
		i32 start_idx = chunk_elems - 4;
		i32 offset = size - start_idx;
		T* res = new (NewPlaceholder(), chunks[chunk] + offset) T(static_cast<Args&&>(args)...);
		++size;
		return *res;
	}

	T& operator[](i32 idx) {
		unsigned long m = (idx + 4) >> 2;              // Normalize so first 4 indices fall into chunk 0; divides by 4 with bias.
		unsigned long chunk = 0;
		_BitScanReverse(&chunk, (unsigned long)m);     // Find position of highest set bit => selects chunk (log2).
		i32 chunk_elems = (i32(4) << chunk);           // Chunk size = 4 * 2^chunk (sequence: 4,8,16,32,...).
		i32 start_idx = chunk_elems - 4;               // First global index served by this chunk.
		i32 offset = idx - start_idx;                  // In-chunk offset of requested element.
		return chunks[chunk][offset];                  // Return reference to the element within the chunk.
	}

	T& last() { return (*this)[size - 1]; }

	void grow() {
		if (size < capacity) return;

		for (int i = 0; i < 20; ++i) {
			if (!chunks[i]) {
				i32 chunk_elems = (i32(4) << i);
				chunks[i] = (T*)allocator.allocate(chunk_elems * sizeof(T));
				capacity += chunk_elems;
				return;
			}
		}
	}
	
	struct Iterator {
		ExpArray* array;
		i32 idx;

		bool operator ==(const Iterator& rhs) const {
			return array == rhs.array && idx == rhs.idx;
		}

		void operator++() {
			++idx;
		}

		T& operator*() {
			return (*array)[idx];
		}
	};

	Iterator begin() { return { this, 0 }; }
	Iterator end() { return { this, size }; }

	IAllocator& allocator;
	T* chunks[20] = {};
	i32 size = 0;
	i32 capacity = 0;
};

struct ArenaAllocator : IAllocator {
	static constexpr size_t CAPACITY = 1024*1024*1024;
	ArenaAllocator() {
		mem = VirtualAlloc(nullptr, CAPACITY, MEM_RESERVE, PAGE_READWRITE);
	}

	~ArenaAllocator() {
		VirtualFree(mem, 0, MEM_RELEASE);
	}
	
	void* allocate(size_t size) override {
		if (allocated + size > commited) {
			static constexpr size_t PAGE_SIZE = 4096;
			size_t required = allocated + size;
			size_t new_commited = (required + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
			if (new_commited > CAPACITY) return nullptr;
			if (!VirtualAlloc((char*)mem + commited, new_commited - commited, MEM_COMMIT, PAGE_READWRITE)) return nullptr;
			commited = new_commited;
		}

		allocated += size;
		return (char*)mem + allocated - size;
	}

	void deallocate(void* mem) override {}

	void* mem;
	size_t allocated = 0;
	size_t commited = 0;
};

struct StringView {
	const char* begin = nullptr;
	const char* end = nullptr;

	i32 size() const { return i32(end - begin); }
	char operator[] (i32 index) { return begin[index]; }
};

static StringView makeStringView(const char* str) {
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

template <typename T>
struct Span {
	T* begin;
	T* end;
};

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
	OutputDebugString(buffer);
}

struct FileIterator {
	WIN32_FIND_DATAA ffd;
	HANDLE handle;
	bool is_valid;
};

struct FileInfo {
	bool is_directory;
	char name[MAX_PATH];
};

static bool getNextFile(FileIterator& iterator, FileInfo& info) {
	if (!iterator.is_valid) return false;

	info.is_directory = (iterator.ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	
	buildString(info.name, iterator.ffd.cFileName);

	iterator.is_valid = FindNextFile(iterator.handle, &iterator.ffd) != FALSE;
	return true;
}


static void destroyFileIterator(FileIterator iterator) {
	FindClose(iterator.handle);
}

static FileIterator createFileIterator(StringView path) {
	char pattern[MAX_PATH];
	buildString(pattern, path, "/*");
	
	FileIterator iter;
	iter.handle = FindFirstFileA(pattern, &iter.ffd);
	iter.is_valid = iter.handle != INVALID_HANDLE_VALUE;
	return iter;
}

static bool startsWith(StringView str, const char* prefix) {
	const char* c = prefix;
	const char* d = str.begin;
	while (*c && d != str.end && *c == *d) {
		++c;
		++d;
	}
	return !*c;
}

static bool startsWith(StringView str, StringView prefix) {
	const char* c = prefix.begin;
	const char* d = str.begin;
	while (c != prefix.end && d != str.end && *c == *d) {
		++c;
		++d;
	}
	return c == prefix.end;
}

static bool equal(StringView lhs, const char* rhs) {
	const char* a = lhs.begin;
	const char* b = rhs;
	while (*b && a != lhs.end && *a == *b) {
		++a;
		++b;
	}
	return !*b && a == lhs.end;
}

static bool equal(StringView lhs, StringView rhs) {
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

StringView consumeArgs(StringView& str) {
	str = skipWhitespaces(str);
	StringView args;
	args.begin = str.begin;
	args.end = args.begin;
	if (args.begin == str.end) return args;
	if (*args.begin != '(') return args;
	while (args.end != str.end && *args.end != ')') ++args.end;
	if (args.end != str.end) ++args.end; // include ')'
	str.begin = args.end;

	// trim "()"
	if (args.size() > 1) {
		++args.begin;
		--args.end;
	}
	return args;
}

struct OutputStream {
	OutputStream() {
		data = new char[capacity];
	}
	~OutputStream() {
		delete[] data;
	}

	void consume(OutputStream& rhs) {
		delete[] data;
		data = rhs.data;
		capacity = rhs.capacity;
		length = rhs.length;
		rhs.data = nullptr;
		rhs.capacity = 0;
		rhs.length = 0;
	}

	template <typename... Args>
	void add(Args... args) {
		(append(args), ...);
	}

	void append(const char* v) {
		append(makeStringView(v));
	}

	void append(XXH64_hash_t hash) {
		char cstr[32] = "";
		_ui64toa_s(hash, cstr, sizeof(cstr), 10);
		append(makeStringView(cstr));
	}

	void append(i32 value) {
		char cstr[32] = "";
		_itoa_s(value, cstr, 10);
		append(makeStringView(cstr));
	}

	void reserve(i32 size) {
		if (capacity >= size) return;

		capacity = size;
		char* new_data = new char[capacity];
		memcpy(new_data, data, length);
		delete[] data;
		data = new_data;
	}

	void append(StringView v) {
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

	char* data;
	i32 capacity = 64 * 1024;
	i32 length = 0;
};

struct Attributes {
	StringView label;
	StringView min;
	StringView clamp_max;
	StringView resource_type;
	StringView property_name;
	StringView dynamic_enum_name;
	bool no_ui = false;
	bool is_radians = false;
	bool is_color = false;
	bool force_function = false;
	bool force_getter = false;
	bool force_setter = false;
	bool is_multiline = false;
};

struct Property {
	StringView name;
	StringView type;
	StringView getter_name;
	StringView setter_name;
	StringView getter_args;
	StringView setter_args;
	Attributes attributes;
	bool is_var = false;
};

struct Function {
	StringView return_type;
	StringView name;
	StringView args;
	Attributes attributes;
};

struct ArrayProperty {
	ArrayProperty(IAllocator& allocator) : children(allocator) {}
	StringView id;
	StringView name;
	ExpArray<Property> children;
};

struct Component {
	Component(IAllocator& allocator)
		: functions(allocator)
		, properties(allocator)
		, arrays(allocator)
	{}
	// name used to detect properties, e.g. if name =="Decal", function `void setDecalMaterialPath(...);` is
	// considered setter for property `MaterialPath` on component `Decal`
	StringView name;
	StringView struct_name;		// e.g., it's `NavmeshZone` in case of `struct NavmeshZone { ...`
	StringView id;				// ComponentType
	StringView label;			// how it's shown in UI
	StringView icon;
	ExpArray<Function> functions;
	ExpArray<Property> properties;
	ExpArray<ArrayProperty> arrays;
};

struct Enumerator {
	StringView name;
	i32 value;
};

struct Enum {
	Enum(IAllocator& allocator) : values(allocator) {}
	StringView name;
	StringView full;
	ExpArray<Enumerator> values;
};

struct StructVar {
	StringView type;
	StringView name;
};

struct Object {
	Object(IAllocator& allocator) : functions(allocator) {}
	StringView name;
	char* filename;
	ExpArray<Function> functions;
};

struct Struct {
	Struct(IAllocator& allocator) : vars(allocator) {}
	StringView name;
	ExpArray<StructVar> vars;
};


struct Module {
	Module(IAllocator& allocator) 
		: components(allocator)
		, functions(allocator)
		, events(allocator)
		, enums(allocator)
		, includes(allocator)
		{}
	StringView name;
	StringView id;
	StringView label;
	char* filename;
	ExpArray<Component> components;
	ExpArray<Function> functions;
	ExpArray<StringView> events;
	ExpArray<Enum> enums;
	ExpArray<StringView> includes;
};

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

	Function consumeCPPFunction(StringView& str) {
		Function res;
		res.return_type = consumeType(str);
		res.name = consumeIdentifier(str);
		res.args = consumeArgs(str);
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
		}
	}

	void parseFunctions() {
		StringView line;
		while (readLine(line)) {
			StringView word = consumeWord(line);
			if (equal(word, "virtual")) {
				Function fn = consumeCPPFunction(line);
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
		e.full = full.size() > 0 ? full : enum_name;
		e.name = enum_name;
		last_enumerator_value = -1;

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
				e.value = atoi(tmp);
				last_enumerator_value = e.value;
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

	void parseObject(StringView def) {
		StringView word = consumeWord(def);
		StringView name;
		while (word.size() > 0) {
			if (equal(word, "name")) {
				name = consumeIdentifier(def);
			}
			else {
				logError("Unexpected ", word);
			}
			word = consumeWord(def);
		}

		StringView line;
		if (!readLine(line)) {
			logError("Expected struct");
			return;
		}
		word = consumeWord(line);
		if (!equal(word, "struct")) {
			logError("Expected struct");
			return;
		}
		if (name.size() == 0) name = consumeIdentifier(line);

		Object& o = objects.emplace(allocator);
		o.name = name;
		o.filename = (char*)allocator.allocate(filename.size() + 1);
		strncpy_s(o.filename, filename.size() + 1, filename.begin, filename.size());

		while (readLine(line)) {
			word = consumeWord(line);
			if (equal(word, "//@")) {
				word = consumeWord(line);
				if (equal(word, "function")) {
					if (!readLine(line)) {
						logError("Expected new line with function");
						continue;
					}
					Function& func = o.functions.emplace();
					word = consumeWord(line);
					if (!equal(word, "virtual")) {
						logError("Expected virtual");
					}
					func.return_type = consumeType(line);
					func.name = consumeIdentifier(line);
					func.args = consumeArgs(line);
				}
				else {
					logError("Unexpected ", word);
				}
			}
		}
	}

	void parseStruct(StringView def) {
		StringView word = consumeWord(def);
		StringView name;
		while (word.size() > 0) {
			if (equal(word, "name")) {
				name = consumeIdentifier(def);
			}
			else {
				logError("Unexpected ", word);
			}
			word = consumeWord(def);
		}

		StringView line;
		if (!readLine(line)) {
			logError("Expected struct");
			return;
		}
		word = consumeWord(line);
		if (!equal(word, "struct")) {
			logError("Expected struct");
			return;
		}
		if (name.size() == 0) name = consumeIdentifier(line);
		Struct& s = structs.emplace(allocator);
		s.name = name;

		while (readLine(line)) {
			line = skipWhitespaces(line);
			
			if (peekChar(line) == '}') break;

			StringView type = consumeType(line);
			if (equal(type, "using")) {
				continue;
			}
			StructVar& v = s.vars.emplace();
			v.type = type;
			v.name = consumeIdentifier(line);
		}
	}

	void parse() {
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
	i32 last_enumerator_value = -1;
	Component* current_component = nullptr;
	Module* current_module = nullptr;
	ExpArray<Module> modules;
	ExpArray<Struct> structs;
	ExpArray<Object> objects;
	ExpArray<Enum> enums;
	StringView content;
	i32 line_idx = 0;
};

ArenaAllocator allocator;
Parser parser(allocator);

void parseFile(StringView path, StringView filename) {
	char full[MAX_PATH];
	buildString(full, path, "/", filename);

	HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;
	defer { CloseHandle(h); };

	DWORD size = GetFileSize(h, nullptr);
	if (size == INVALID_FILE_SIZE) return;

	char* data = (char*)allocator.allocate(size + 1);
	DWORD read = 0;
	if (ReadFile(h, data, size, &read, nullptr)) {
		data[read] = 0;
	}

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
			parseFile(path, name);
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

Struct* getStruct(StringView name) {
	for (Struct& s : parser.structs) {
		if (equal(s.name, name)) return &s;
	}
	return nullptr;
}

Object* getObject(StringView name) {
	if (*(name.end - 1) == '*') {
		--name.end;
	}
	for (Object& o : parser.objects) {
		if (equal(o.name, name)) return &o;
	}
	return nullptr;
}

Enum* getEnum(Module& m, StringView name) {
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

struct Arg {
	StringView type;
	StringView name;
	bool is_const = false;
	bool is_ref = false;
};

static bool endsWith(StringView str, const char* prefix) {
	i32 plen = (i32)strlen(prefix);
	if (plen > str.size()) return false;

	const char* a = str.end - plen;
	const char* b = prefix;
	while (*b) {
		if (*a != *b) return false;
		++a;
		++b;
	}
	return true;
}

bool consumeArg(StringView& line, Arg& out) {
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
	out.type = word;
	word = consumeWord(line);
	out.name = word;
	return true;
}

template <typename F>
void forEachArg(StringView args, F f) {
	Arg arg;
	bool first_arg = true;
	while (consumeArg(args, arg)) {
		f(arg, first_arg);
		first_arg = false;
	}
}

Struct* findStruct(StringView struct_name) {
	for (Struct& s : parser.structs) {
		if (equal(s.name, struct_name)) return &s;
	}
	return nullptr;
}

Object* findObject(StringView object_name) {
	for (Object& o : parser.objects) {
		if (equal(o.name, object_name)) return &o;
	}
	return nullptr;
}

void wrap(OutputStream& out, Module& m, Function& f) {
	L("int ",m.name,"_",f.name,"(lua_State* L) {");
	L("LuaWrapper::checkTableArg(L, 1);");
	L(m.name,"* module;");
	L("if (!LuaWrapper::checkField(L, 1, \"_module\", &module)) luaL_argerror(L, 1, \"Module expected\");");

	i32 arg_idx = -1;
	StringView args = f.args;
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		++arg_idx;
		Struct* st = findStruct(arg.type);
		if (st) {
			L("\t",arg.type," ",arg.name,";" OUT_ENDL);
			for (StructVar& v : st->vars) {
				L("\tif(!LuaWrapper::checkField(L, ",(arg_idx + 2),", \"",v.name,"\", &",arg.name,".",v.name,")) luaL_error(L, \"Invalid argument\");");
			}
		}
		else {
			Object* obj = findObject(arg.type);
			if (obj) {
				L("\tif(!LuaWrapper::checkField(L, ",(arg_idx + 2),", \"_object\", &",arg.name,")) luaL_error(L, \"Invalid argument\");");
			}
			else if (arg.is_const && equal(arg.type, "char*")) {
				L("\tauto ",arg.name," = LuaWrapper::checkArg<const char*>(L, ",(arg_idx + 2),");");
			}
			else {
				L("\tauto ",arg.name," = LuaWrapper::checkArg<",arg.type,">(L, ",(arg_idx + 2),");");
			}
		}
	});

	Struct* st = findStruct(f.return_type);
	Object* obj = findObject(withoutSuffix(f.return_type, 1)/*remove pointer '*`*/);
	if (st) out.add("\tauto s = ");
	bool has_return = !equal(f.return_type, "void") && st == nullptr;
	if (obj) out.add("\tLuaWrapper::pushObject(L, ");
	else if (has_return) out.add("\tLuaWrapper::push(L, ");
	out.add("\tmodule->",f.name,"(");
	forEachArg(args, [&](const Arg& arg, bool is_first){
		if (!is_first) out.add(", ");
		out.add(arg.name);
	});
	out.add(")");
	if (obj) {
		L(", \"",withoutSuffix(f.return_type, 1),"\");");
		L("return 1;");
	}
	else if (st) {
		L(";");
		L("\tlua_newtable(L);");
		for (StructVar& v : st->vars) {
			L("\tLuaWrapper::push(L, s.",v.name,");");
			L("\tlua_setfield(L, -2, \"",v.name,"\");");
		}
		L("\treturn 1;");
	}
	else if (has_return) {
		L(");" OUT_ENDL "\treturn 1;");
	}
	else {
		L(";" OUT_ENDL "\treturn 0;");
	}
	L("}" OUT_ENDL);
}

void wrap(OutputStream& out, Module& m, Component& c, Function& f) {
	StringView label = f.attributes.label;
	if (label.size() == 0) label = f.name;
	L("int ",c.name,"_",label,"(lua_State* L) {");
	L("\tauto [imodule, entity] = checkComponent(L);");
	L("\tauto* module = (",m.name,"*)imodule;");
	
	i32 arg_idx = -1;
	StringView args = f.args;
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		++arg_idx;
		if (is_first) return; // skip entity, we alredy have it
		if (arg.is_const && equal(arg.type, "char*"))
			L("\tauto ",arg.name," = LuaWrapper::checkArg<const char*>(L, ",(arg_idx + 1),");");
		else
			L("\tauto ",arg.name," = LuaWrapper::checkArg<",arg.type,">(L, ",(arg_idx + 1),");");
	});

	Struct* st = findStruct(f.return_type);
	if (st) out.add("\tauto s = ");
	bool has_return = !equal(f.return_type, "void") && st == nullptr;
	if (has_return) out.add("\tLuaWrapper::push(L, ");
	out.add("\tmodule->",f.name,"(entity");
	forEachArg(args, [&](const Arg& arg, bool is_first){
		if (is_first) return;
		out.add(", ", arg.name);
	});
	out.add(")");
	if (st) {
		L(";");
		L("\tlua_newtable(L);");
		for (StructVar& v : st->vars) {
			L("\tLuaWrapper::push(L, s.",v.name,");");
			L("\tlua_setfield(L, -2, \"",v.name,"\");");
		}
		L("" OUT_ENDL "\treturn 1;");
	}
	else if (has_return) {
		L(");" OUT_ENDL "\treturn 1;");
	}
	else {
		L(";" OUT_ENDL "\treturn 0;");
	}
	L("}", OUT_ENDL);
}

void wrap(OutputStream& out, StringView module, StringView component, StringView property_name, StringView method_name, StringView args, bool is_getter) {
	L("int ", (is_getter ? "get" : "set"), component, property_name, "(lua_State* L) {");
	L("\tauto [imodule, entity] = checkComponent(L);");
	L("\tauto* module = (",module,"*)imodule;");
	
	i32 idx = 2;
	forEachArg(args, [&](const Arg& arg, bool){
		L("\tauto ", arg.name, " = LuaWrapper::checkArg<",arg.type,">(L, ",idx,");");
		++idx;
	});
	if (is_getter) out.add("\tLuaWrapper::push(L, module->", method_name, "(");
	else out.add("\tmodule->", method_name, "(");
	forEachArg(args, [&](const Arg& arg, bool first){
		if (!first) out.add(", ");
		out.add(arg.name);
	});
	if (is_getter) out.add(")");
	L(");");
	if (is_getter) L("\treturn 1;");
	else L("\treturn 0;");
	L("}" OUT_ENDL);
}

StringView pickLabel(StringView base, StringView spec) {
	if (spec.size() > 0) return spec;
	return base;
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

void serializeMain(OutputStream& out, Parser& parser) {
	L("namespace Lumix {" OUT_ENDL);
	L("void registerLuaAPI(lua_State* L) {");
	L("	lua_newtable(L);");
	L("	lua_setglobal(L, \"LumixModules\");");

	for (Module& m : parser.modules) {
		if (m.functions.size == 0) continue;
		L("{");
		L("	lua_newtable(L);");
		L("	lua_getglobal(L, \"LumixModules\");");
		L("	lua_pushvalue(L, -2);");
		L("	lua_setfield(L, -2, \"",m.id,"\");");
		L("	lua_pop(L, 1);");
		L("	lua_pushvalue(L, -1);");
		L("	lua_setfield(L, -2, \"__index\");");
		L("	lua_pushcfunction(L, lua_new_module, \"new\");");
		L("	lua_setfield(L, -2, \"new\");");
		for (Function& f : m.functions) {
			L("lua_pushcfunction(L, ",m.name,"_",f.name,", \"",f.name,"\");");
			L("lua_setfield(L, -2, \"",pickLabel(f.name, f.attributes.label),"\");");
		}
		L("	lua_pop(L, 1);");
		L("}");
	}

	for (Object& o : parser.objects) {
		L("{");
		L("lua_getglobal(L, \"LumixAPI\");");
		L("lua_newtable(L);");
		L("lua_pushvalue(L, -1);");
		L("lua_setfield(L, -3, \"",o.name,"\");");
		L("lua_pushvalue(L, -1);");
		L("lua_setfield(L, -2, \"__index\");");

		for (Function& f : o.functions) {
			L("{");
			L("auto proxy = [](lua_State* L) -> int {");
				L("LuaWrapper::checkTableArg(L, 1); // self");
				L(o.name, "* obj;");
				L("if (!LuaWrapper::checkField(L, 1, \"_value\", &obj)) luaL_error(L, \"Invalid object\");");
				
				i32 idx = 0;
				forEachArg(f.args, [&](const Arg& arg, bool){
					++idx;
					L("auto ",arg.name," = LuaWrapper::checkArg<",arg.type,">(L, ",(idx + 1),");");
				});
				
				bool has_return = !equal(f.return_type, "void");
				if (has_return) out.add("auto res = ");
				
				out.add("obj->",f.name,"(");
				forEachArg(f.args, [&](const Arg& arg, bool first){
					if (!first) out.add(", ");
					out.add(arg.name);
				});
				L(");");
				
				if (has_return) {
					L("LuaWrapper::push(L, res);");
					L("return 1;");
				}
				else {
					L("return 0;");
				}
				
			L("};");

			L("const char* name = \"", f.name, "\";");
			L("lua_pushcfunction(L, proxy, name);");
			L("lua_setfield(L, -2, name);");
			L("}");
		}
		L("lua_pop(L, 2);");
		L("}");
	}
	
	for (Module& m : parser.modules) {
		for (Component& c : m.components) {
			L("\tregisterLuaComponent(L, \"",c.id,"\", ",c.name,"_getter, ",c.name,"_setter);");
		}
	}
	L("}");
	L("}" OUT_ENDL);

	formatCPP(out);
}

bool isBlob(const Property& p) {
	return equal(p.type, "void");
}

void serializeLuaPropertySetter(OutputStream& out, Module& m, Component& c) {
	L("int ",c.name,"_setter(lua_State* L) {");
	L("auto [imodule, entity] = checkComponent(L);");
	L("auto* module = (",m.name,"*)imodule;");
	L("const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);");
	L("XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));");
	L("switch (name_hash) {");
	
	bool is_array = false;
	for (Property& p : c.properties) {
		if (isBlob(p)) continue;

		// TODO check collisions
		XXH64_hash_t hash = XXH3_64bits(p.name.begin, p.name.size());
		if (p.is_var) {
			L("case /*",p.name,"*/",hash,": module->get",c.name,"(entity).",p.name," = LuaWrapper::checkArg<",p.type,">(L, 3); break;");
			continue;
		}
		
		if (p.getter_name.size() == 0) continue;
		if (p.setter_name.size() == 0) continue;
		
		char tmp[256];
		toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 255));

		hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash,": ");
		if (Enum* e = getEnum(m, p.type)) {
			L("module->",p.setter_name,"(entity, (",e->full,")LuaWrapper::checkArg<i32>(L, 3)); break;");
		}
		else {
			L("module->",p.setter_name,"(entity, LuaWrapper::checkArg<",p.type,">(L, 3)); break;");
		}
	}
	L("case 0:"); // to avoid emtpy switch (compiler error) in case we have 0 properties
	L("default: ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); break;");
	L("}");

	L("return 0;");
	L("}" OUT_ENDL);
}

void serializeLuaArrayGetter(OutputStream& out, Module& m, Component& c, ArrayProperty& a) {
	L("using GetterModule = ",m.name,";");
	out.add(R"#(auto getter = [](lua_State* L) ->int {
		LuaWrapper::checkTableArg(L, 1); // self
		auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
		EntityRef entity{LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
		if (lua_type(L, 2) == LUA_TSTRING) {
			auto adder = [](lua_State* L) -> int  {
				auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
				EntityRef entity{LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
				module->add)#",a.name,R"#((entity, module->get)#",a.name,R"#(Count(entity));
				return 0;
			};

			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			if (equalStrings(prop_name, "add")) {
				LuaWrapper::push(L, module);
				LuaWrapper::push(L, entity.index);
				lua_pushcclosure(L, adder, "adder", 2);
				return 1;
			}
			else {
				ASSERT(false);
				luaL_error(L, "Unknown property %s", prop_name);
			}
		}

		auto getter = [](lua_State* L) -> int {
			LuaWrapper::checkTableArg(L, 1);
			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
			EntityRef entity = {LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
			i32 index = LuaWrapper::toType<int>(L, lua_upvalueindex(3));
			XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));
			switch (name_hash) {
	)#");

	for (Property& child : a.children) {
		if (isBlob(child)) continue;
		if (child.getter_name.size() == 0) continue;
		
		char tmp[256];
		toID(pickLabel(child.name, child.attributes.label), Span(tmp, tmp + 256));
		XXH64_hash_t hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash,": ");
		if (getEnum(m, child.type)) {
			L("LuaWrapper::push(L, (i32)module->",child.getter_name,"(entity, index)); break;");
		}
		else {
			L("LuaWrapper::push(L, module->",child.getter_name,"(entity, index)); break;");
		}
	}
	L("default: { ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); break; }");
	L("}");

	out.add(R"#(return 1;
		};

		auto setter = [](lua_State* L) -> int {
			LuaWrapper::checkTableArg(L, 1);
			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));
			auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
			EntityRef entity = {LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
			i32 index = LuaWrapper::toType<int>(L, lua_upvalueindex(3));
			switch (name_hash) {
	)#");	

	for (Property& child : a.children) {
		if (isBlob(child)) continue;
		if (child.setter_name.size() == 0) continue;
		
		char tmp[256];
		toID(pickLabel(child.name, child.attributes.label), Span(tmp, tmp + 256));

		XXH64_hash_t hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash,": ");
		if (Enum* e = getEnum(m, child.type)) {
			L("module->",child.setter_name,"(entity, index, (",e->full,")LuaWrapper::checkArg<i32>(L, 3)); break;");
		}
		else {
			L("module->",child.setter_name,"(entity, index, LuaWrapper::checkArg<",child.type,">(L, 3)); break;");
		}
	}

	out.add(R"#(
			case 0:	
			default: { ASSERT(false); luaL_error(L, "Unknown property %ss", prop_name); break; }
			}
			return 0;
			};

			i32 index = LuaWrapper::checkArg<i32>(L, 2) - 1;
			i32 num_elements = module->get)#",a.name,R"#(Count(entity);
			if (index >= num_elements) {
				lua_pushnil(L);
				return 1;
			}

			lua_newtable(L);
			lua_newtable(L);

			lua_pushlightuserdata(L, (void*)module);
			LuaWrapper::push(L, entity.index);
			LuaWrapper::push(L, index);
			lua_pushcclosure(L, getter, "getter", 3);
			lua_setfield(L, -2, "__index");

			lua_pushlightuserdata(L, (void*)module);
			LuaWrapper::push(L, entity.index);
			LuaWrapper::push(L, index);
			lua_pushcclosure(L, setter, "setter", 3);
			lua_setfield(L, -2, "__newindex");

			lua_setmetatable(L, -2);
			return 1;
		};

		lua_newtable(L); // {}
		lua_newtable(L); // {}, metatable
		LuaWrapper::push(L, module);
		LuaWrapper::push(L, entity.index);
		lua_pushcclosure(L, getter, "getter", 2);
		lua_setfield(L, -2, "__index"); // {}, mt
		lua_setmetatable(L, -2); // {}
	)#");
}

// TODO move lua stuff into separate file
void serializeLuaPropertyGetter(OutputStream& out, Module& m, Component& c) {
	L("int ",c.name,"_getter(lua_State* L) {");
	L("\tauto [imodule, entity] = checkComponent(L);");
	L("\tauto* module = (",m.name,"*)imodule;");

	if (equal(c.id, "lua_script")) {
		L("if (lua_isnumber(L, 2)) return lua_push_script_env(L, entity, module);");
	}

	L("const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);");
	L("XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));");
	L("switch (name_hash) {");

	for (ArrayProperty& a : c.arrays) {
		XXH64_hash_t hash = XXH3_64bits(a.id.begin, a.id.size());
		L("case /*",a.id,"*/",hash, ": {");
		serializeLuaArrayGetter(out, m, c, a);
		L("break;");
		L("}");
	}

	for (Property& p : c.properties) {
		if (isBlob(p)) continue;
		if (p.is_var) {
			XXH64_hash_t hash = XXH3_64bits(p.name.begin, p.name.size());
			out.add("case /*",p.name,"*/",hash, ": ");
			L("LuaWrapper::push(L, module->get",c.name,"(entity).",p.name,"); break;");
			continue;
		}
		if (p.getter_name.size() == 0) continue;

		char tmp[256];
		toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 255));
		XXH64_hash_t hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash, ": ");
		
		if (getEnum(m, p.type)) {
			L("LuaWrapper::push(L, (i32)module->",p.getter_name,"(entity)); break;");
		}
		else {
			L("LuaWrapper::push(L, module->",p.getter_name,"(entity)); break;");
		}
	}

	for (Function& f : c.functions) {
		StringView label = f.attributes.label;
		if (label.size() == 0) label = f.name;
		XXH64_hash_t hash = XXH3_64bits(label.begin, label.size());
		out.add("case /*",label,"*/",hash, ": ");
		L("lua_pushcfunction(L, ",c.name,"_",label,", \"",c.name,"_",f.name,"\"); break;");
	}
	L("case 0:"); // to avoid emtpy switch (compiler error) in case we have 0 properties
	L("default: { ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); break; }");
	L("}");
	L("\treturn 1;");
	L("}" OUT_ENDL);
}

void serializeLuaCAPI(OutputStream& out, Module& m) {
	L("namespace Lumix {");
	for (Function& f : m.functions) {
		wrap(out, m, f);
	}
	for (Component& c : m.components) {
		for (Function& f : c.functions) {
			wrap(out, m, c, f);
		}
		serializeLuaPropertyGetter(out, m, c);
		serializeLuaPropertySetter(out, m, c);
	}
	L("}" OUT_ENDL);
}

StringView toLuaType(StringView ctype) {
	if (equal(ctype, "void")) return makeStringView("()");

	#define C(CTYPE, LUATYPE) do { if (equal(ctype, #CTYPE)) return makeStringView(#LUATYPE); } while (false)
		C(int, number);
		C(const char *, string);
		C(const char*, string);
		C(char const *, string);
		C(Vec3, Vec3);
		C(Quat, Quat);
		C(Vec2, Vec2);
		C(Color, Color);
		C(DVec3, DVec3);
		C(EntityPtr, Entity?);
		C(EntityRef, Entity);
		C(Path, string);
		C(i32, number);
		C(u32, number);
		C(float, number);
		C(bool, boolean);
	#undef C

	// TODO structs	
	Struct* s = getStruct(ctype);
	if (s) return s->name;

	Object* o = getObject(ctype);
	if (o) return o->name;

	return makeStringView("any");
}

void serializeLuaType(OutputStream& out, StringView self_type, const char* self_type_suffix, Function& f, bool skip_first_arg) {
	out.add("\t",pickLabel(f.name, f.attributes.label),": (");
	forEachArg(f.args, [&](const Arg& arg, bool first){
		if (!first) out.add(", ");
		if (first) {
			out.add(self_type,self_type_suffix);
			if (!skip_first_arg) {
				out.add(", ", toLuaType(arg.type));
			}
		}
		else {
			out.add(toLuaType(arg.type));
		}
	});
	out.add(") -> ",toLuaType(f.return_type),OUT_ENDL);
}

void serializeLuaTypes(OutputStream& out_formatted) {
	OutputStream out;
	out.add(R"#(
	export type Vec2 = {number}
	export type Vec3 = {number}
	export type Color = {number}
	export type Quat = {number}
	export type DVec3 = {number}
	declare ImGui: {
		AlignTextToFramePadding : () -> (),
		Begin : (string, boolean?) -> (boolean, boolean?),
		BeginChildFrame : (string, number, number) -> boolean,
		BeginMenu : (string, boolean) -> boolean,
		BeginPopup : (string) -> boolean,
		Button : (string) -> boolean,
		CalcTextSize : (string) -> (number, number),
		Checkbox : (string, boolean) -> (boolean, boolean),
		CloseCurrentPopup : () -> (),
		CollapsingHeader : (string) -> boolean,
		Columns : (number) -> (),
		DragFloat : (string, number) -> (boolean, number),
		DragInt : (string, number) -> (boolean, number),
		Dummy : (number, number) -> (),
		End : () -> (),
		EndChildFrame : () -> (),
		EndCombo : () -> (),
		EndMenu : () -> (),
		EndPopup : () -> (),
		GetColumnWidth : (number) -> number,
		GetDisplayWidth : () -> number,
		GetDisplayHeight : () -> number,
		GetOsImePosRequest : () -> (number, number),
		GetWindowWidth : () -> (),
		GetWindowHeight : () -> (),
		GetWindowPos : () -> any,
		Indent : (number) -> (),
		InputTextMultiline : (string, string) -> (boolean, string?),
		InputTextMultilineWithCallback : (string, string, (string, number, boolean) -> ()) -> (boolean, string?),
		IsItemHovered : () -> boolean,
		IsKeyPressed : (number, boolean) -> boolean,
		IsMouseClicked : (number) -> boolean,
		IsMouseDown : (number) -> boolean,
		LabelText : (string, string) -> (),
		NewLine : () -> (),
		NextColumn : () -> (),
		OpenPopup : (string) -> (),
		PlotLines : (string, {number}, Vec2) -> (),
		PopItemWidth : () -> (),
		PopID : () -> (),
		PopStyleColor : (number) -> (),
		PopStyleVar : (number) -> (),
		PopItemWidth : () -> (),
		PushItemWidth : (number) -> (),
		PushID : (number) -> (),
		PushStyleColor : (number, any) -> (),
		PushStyleVar : (number, number, number) -> () | (number, number) -> () ,
		Rect : (number, number, number) -> (),
		SameLine : () -> (),
		Selectable : (string, boolean) -> boolean | (string) -> boolean,
		Separator : () -> (),
		SetCursorScreenPos : (number, number) -> (),
		SetKeyboardFocusHere : (number) -> (),
		SetNextWindowPos : (number, number) -> (),
		SetNextWindowPosCenter : () -> (),
		SetNextWindowSize : (number, number) -> (),
		SetStyleColor : (number, any) -> (),
		SliderFloat : (string, number, number, number) -> (boolean, number),
		Text : (string) -> (),
		Unindent : (number) -> (),

		Key_DownArrow : number,
		Key_Enter : number,
		Key_Escape : number,
		Key_UpArrow : number
	}

	declare class World
		getActivePartition : (World) -> number
		setActivePartition : (World, number) -> ()
		createPartition : (World, string) -> number
		load : (World, string, any) -> ()
		getModule : (string) -> any
		createEntity : () -> Entity
		createEntityEx : (any) -> Entity
		findEntityByName : (string) -> Entity
	)#");

	for (Module& m : parser.modules) {
		L(m.id,": ",m.id,"_module");
	}

	L("end" OUT_ENDL);

	for (Struct& s : parser.structs) {
		L("declare class ",s.name);
		for (StructVar& v : s.vars) {
			L(v.name,": ",toLuaType(v.type));
		}
		L("end" OUT_ENDL);
	}

	for (Object& o : parser.objects) {
		L("declare class ",o.name);
		for (Function& f : o.functions) {
			serializeLuaType(out, o.name, "", f, false);
		}
		L("end" OUT_ENDL);
	}

	for (Module& m : parser.modules) {
		L("declare class ",m.id,"_module");
		for (Function& f : m.functions) {
			serializeLuaType(out, m.id, "_module", f, false);
		}
		L("end" OUT_ENDL);
		
		for (Component& c : m.components) {
			L("declare class ",c.id,"_component");
			for (Property& p : c.properties) {
				
				char tmp[256];
				toID(p.name, Span(tmp, tmp + 256));
				if (!isBlob(p) && p.type.size() > 0) L("	",tmp,": ", toLuaType(p.type));
			}
			for (Function& f : c.functions) {
				serializeLuaType(out, c.id, "_component", f, true);
			}
			L("end" OUT_ENDL);
		}
	}

	out.add(R"#(
	declare class Entity 
		world : World
		name : string
		parent : Entity?
		rotation : any
		position : Vec3
		scale : Vec3
		hasComponent : (Entity, any) -> boolean
		getComponent : (Entity, any) -> any
		destroy : (Entity) -> ()
		createComponent : (Entity, any) -> any
	)#");

	for (Module& m : parser.modules) {
		for (Component& c : m.components) {
			L(c.id,": ",c.id,"_component");
		}
	}
	
	L("end" OUT_ENDL);

	out.add(R"#(
	declare this:Entity

	type ActionDesc = {
		name : string,
		label : string,
		run : () -> ()
	}

	declare Editor: {
		RESOURCE_PROPERTY : number,
		COLOR_PROPERTY : number,
		ENTITY_PROPERTY : number,
		BOOLEAN_PROPERTY : number,
		setPropertyType : (any, string, number, string?) -> (),
		setArrayPropertyType : (any, string, number, string?) -> (),
		getSelectedEntitiesCount : () -> number,
		getSelectedEntity : (number) -> Entity,
		addAction : (ActionDesc) -> (),
		createEntityEx : (any) -> Entity,
		scene_view : SceneView,
		asset_browser : AssetBrowser
	}

	declare LumixAPI: {
		RaycastHit : { create : () -> RaycastHit, destroy : (RaycastHit) -> () },
		SweepHit : { create : () -> SweepHit, destroy : (SweepHit) -> () },
		Ray : { create : () -> Ray, destroy : (Ray) -> () },
		RayCastModelHit : { create : () -> RayCastModelHit, destroy : (RayCastModelHit) -> () },

		INPUT_KEYCODE_SHIFT: number,
		INPUT_KEYCODE_LEFT : number,
		INPUT_KEYCODE_RIGHT : number,
		engine : any,
		logError : (string) -> (),
		logInfo : (string) -> (),
		loadResource : (any, path:string, restype:string) -> any,
		writeFile : (string, string) -> boolean
	}

	type InputDevice = {
		type : "mouse" | "keyboard",
		index : number
	}

	type AxisInputEvent = {
		type : "axis",
		device : InputDevice,
		x : number,
		y : number,
		x_abs : number,
		y_abs : number
	}

	type ButtonInputEvent = {
		type : "button",
		device : InputDevice,
		key_id : number,
		down : boolean,
		is_repeat : boolean,
		x : number,
		y : number
	}

	export type InputEvent = ButtonInputEvent | AxisInputEvent
	)#");

	// format output
	StringView raw(out.data, out.data + out.length);
	StringView line;
	i32 indent = 0;
	const char* tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	while (readLine(raw, line)) {
		i32 prev_indent = indent;
		StringView word = peekWord(line);
		if (equal(word, "declare")) {
			++indent;
		}
		else if (equal(word, "end")) {
			--indent;
		}
		else {
			for (const char* c = line.begin; c < line.end; ++c) {
				if (*c == '{') ++indent;
				else if (*c == '}') --indent;
			}
		}

		if (indent > 0) out_formatted.add(StringView(tabs, tabs + (indent < prev_indent ? indent : prev_indent)));
		out_formatted.add(line, OUT_ENDL);
	}
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
		StringView label = fn.name;
		if (fn.attributes.label.size() > 0) label = fn.attributes.label;
		L("\t.function<(", fn.return_type, " (", m.name, "::*)(", fn.args, "))&", m.name, "::", fn.name ,">(\"", label, "\")");
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
			StringView label = fn.attributes.label.size() > 0 ? fn.attributes.label : fn.name;
			L("\t\t.function<(", fn.return_type, " (", m.name, "::*)(", fn.args, "))&", m.name, "::", fn.name, ">(\"", label, "\")");
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
	// skip writing if file exists and content is identical
	HANDLE h_existing = CreateFileA(out_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h_existing != INVALID_HANDLE_VALUE) {
		defer { CloseHandle(h_existing); };
		DWORD existing_size = GetFileSize(h_existing, nullptr);
		if (existing_size == (DWORD)stream.length) {
			char* existing_data = new char[existing_size];
			defer { delete existing_data; };
			DWORD read_bytes = 0;
			if (ReadFile(h_existing, existing_data, existing_size, &read_bytes, nullptr) && read_bytes == existing_size) {
				if (memcmp(existing_data, stream.data, existing_size) == 0) {
					return;
				}
			}
		}
	}
	
	HANDLE hout = CreateFileA(out_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hout != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(hout, stream.data, (DWORD)stream.length, &written, nullptr);
		CloseHandle(hout);
	}
}

int main() {
	LARGE_INTEGER start, stop, freq;
	QueryPerformanceCounter(&start);
	
	scan(makeStringView("."));
	OutputStream stream;
	OutputStream lua_capi_stream;
	OutputStream lua_d_stream;

	lua_capi_stream.add("// Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	lua_d_stream.add("-- Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	for (Object& o : parser.objects) {
		StringView include_path = withoutPrefix(makeStringView(o.filename), 2); // skip "./"
		lua_capi_stream.add("#include \"",include_path,"\"" OUT_ENDL);
	}
	for (Module& m : parser.modules) {
		for (StringView include_path : m.includes) {
			lua_capi_stream.add("#include \"",include_path,"\"" OUT_ENDL);
		}
		StringView include_path = withoutPrefix(makeStringView(m.filename), 2); // skip "./"
		lua_capi_stream.add("#include \"",include_path,"\"" OUT_ENDL);
	}
	lua_capi_stream.add("#define XXH_STATIC_LINKING_ONLY" OUT_ENDL);
	lua_capi_stream.add("#include \"xxhash/xxhash.h\"" OUT_ENDL);

	lua_capi_stream.add(OUT_ENDL);

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

		serializeLuaCAPI(lua_capi_stream, m);
	}
	serializeLuaTypes(lua_d_stream);
	serializeMain(lua_capi_stream, parser);

	writeFile("lua/lua_capi.gen.h", lua_capi_stream);
	writeFile("../data/scripts/lumix.d.lua", lua_d_stream);

	QueryPerformanceCounter(&stop);
	QueryPerformanceFrequency(&freq);
	i32 duration = i32(float((stop.QuadPart - start.QuadPart) / double(freq.QuadPart)) * 1000);
	char tmp[32];
	_itoa_s(duration, tmp, 10);
	logInfo("Processed in ", tmp, " ms");
	return 0;
}