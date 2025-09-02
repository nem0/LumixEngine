#include "core/defer.h"
#include <float.h>
#include <Windows.h>

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
	return args;
}

struct OutputStream {
	OutputStream() {
		data = new char[capacity];
	}
	~OutputStream() {
		delete[] data;
	}

	template <typename... Args>
	void add(Args... args) {
		(append(args), ...);
	}

	void append(const char* v) {
		append(makeStringView(v));
	}

	void append(i32 value) {
		char cstr[32] = "";
		_itoa_s(value, cstr, 10);
		append(makeStringView(cstr));
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
	bool is_enum = false;
	bool is_multiline = false;
	bool is_blob = false;
};

struct Property {
	StringView name;
	StringView type;
	StringView getter_name;
	StringView setter_name;
	StringView array_id;
	StringView getter_args;
	StringView setter_args;
	bool is_var = false;
	bool is_array_begin = false;
	bool is_array_end = false;
	Attributes attributes;
};

struct Function {
	StringView return_type;
	StringView name;
	StringView args;
	Attributes attributes;
};

struct Component {
	Component(IAllocator& allocator)
		: functions(allocator)
		, properties(allocator)
	{}
	StringView name;
	StringView struct_name;
	StringView id;
	StringView label;
	StringView icon;
	ExpArray<Function> functions;
	ExpArray<Property> properties;
};

struct Enumerator {
	StringView name;
	i32 value;
};

struct Enum {
	Enum(IAllocator& allocator) : values(allocator) {}
	StringView name;
	ExpArray<Enumerator> values;
};

struct StructVar {
	StringView type;
	StringView name;
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
	{}
	StringView name;
	StringView id;
	StringView label;
	char* filename;
	ExpArray<Component> components;
	ExpArray<Function> functions;
	ExpArray<StringView> events;
	ExpArray<Enum> enums;
};

struct Parser {
	Parser(IAllocator& allocator)
		: allocator(allocator)
		, modules(allocator)
		, structs(allocator)
	{}

	bool readLine(StringView& content, StringView& line) {
		if (content.size() == 0) return false;
		++line_idx;

		line.begin = content.begin;
		line.end = line.begin;
		
		while (line.end != content.end && *line.end != '\n') {
			++line.end;
		}
		line = skipWhitespaces(line);
		content.begin = line.end;
		if (content.begin != content.end) ++content.begin; // skip \n
		return true;
	}

	template <typename... Args>
	void logError(Args... args) {
		char line_str[8] = "4";
		_itoa_s(line_idx, line_str, 10);
		logInfo(filename, "(", line_str, "): ", args...); // TODO logInfo?
	}

	void parseGetter(StringView return_type, StringView method_name, StringView component_name, StringView name, StringView args, const Attributes* attributes) {
		if (!startsWith(name, component_name)) {
			logError("Expected ", component_name);
			return;
		}
		name.begin += component_name.size();
		getter(return_type, method_name, name, args, attributes);
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
			else if (equal(word, "enum")) {
				attributes.is_enum = true;
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
			else if (equal(word, "blob")) {
				attributes.is_blob = true;
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
		prop.attributes = attributes;
	}

	void parseComponentStruct(StringView name, StringView struct_name, StringView id, StringView label, StringView icon) {
		StringView line;
		beginComponent(name, struct_name, id, label, icon);
		defer { current_component = nullptr; };

		while (readLine(content, line)) {
			StringView def = find(line, "//@");
			if (def.size() > 0) {
				def.begin += 3;
				def = skipWhitespaces(def);
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
		}		
		logError("'//@ end' not found while parsing component ", struct_name);
	}

	StringView withoutPrefix(StringView str, i32 prefix_len) {
		StringView res = str;
		res.begin += prefix_len;
		return res;
	}

	void parseArray(StringView component_name, StringView array_name, StringView array_id) {
		StringView line;

		Property& p = current_component->properties.emplace();
		p.name = array_name;
		p.is_array_begin = true;
		p.array_id = array_id;

		defer { 
			Property& p = current_component->properties.emplace();
			p.is_array_end = true;
		};

		while (readLine(content, line)) {
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
				StringView type = consumeType(line);
				StringView method_name = consumeIdentifier(line);
				StringView args = consumeArgs(line);
				StringView def = find(line, "//@");

				Attributes attributes;
				Attributes* attributes_ptr = nullptr;
				if (def.size() > 0) {
					def.begin += 3;
					if (parseAttributes(def, attributes)) {
						attributes_ptr = &attributes;
					}
				}

				StringView property_name = method_name;
				if (startsWith(method_name, "get")) {
					property_name.begin += 3;
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						if (!equal(withoutPrefix(property_name, array_name.size()), "Count")) {
							getter(type, method_name, property_name, args, attributes_ptr);
						}
					}
				}
				else if (startsWith(method_name, "is")) {
					property_name.begin += 2;
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						if (!equal(withoutPrefix(property_name, array_name.size()), "Count")) {
							getter(type, method_name, property_name, args, attributes_ptr);
						}
					}
				}
				else if (startsWith(method_name, "set")) {
					property_name.begin += 3;
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						setter(method_name, property_name, args, attributes_ptr);
					}
				}
				else if (startsWith(method_name, "enable")) {
					property_name.begin += 6;
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						setter(method_name, property_name, args, attributes_ptr);
					}
				}
			}
		}
		logError("'//@ end' not found while parsing ", component_name, ".", array_name);
	}

	void parseComponent(StringView component_name, StringView id, StringView label, StringView icon) {
		beginComponent(component_name, {}, id, label, icon);
		defer { current_component = nullptr; };

		StringView line;
		while (readLine(content, line)) {
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
				StringView type = consumeType(line);
				StringView method_name = consumeIdentifier(line);
				StringView args = consumeArgs(line);
				StringView def = find(line, "//@");

				Attributes attributes;
				Attributes* attributes_ptr = nullptr;
				if (def.size() > 0) {
					def.begin += 3;
					if (parseAttributes(def, attributes)) {
						attributes_ptr = &attributes;
					}
				}

				StringView property_name = method_name;
				if (attributes.force_function) {
					function(method_name, type, args, attributes_ptr);
				}
				else if (attributes.force_setter) {
					setter(method_name, attributes.property_name, args, attributes_ptr);
				}
				else if (attributes.force_getter) {
					getter(type, method_name, attributes.property_name, args, attributes_ptr);
				}
				else if (startsWith(method_name, "set")) {
					property_name.begin += 3;
					if (!startsWith(property_name, component_name)) {
						logError("Expected ", component_name);
						continue;
					}
					property_name.begin += component_name.size();
					
					setter(method_name, property_name, args, attributes_ptr);
				}
				else if (startsWith(method_name, "get")) {
					property_name.begin += 3;
					parseGetter(type, method_name, component_name, property_name, args, attributes_ptr);
				}
				else if (startsWith(method_name, "is")) {
					property_name.begin += 2;
					parseGetter(type, method_name, component_name, property_name, args, attributes_ptr);
				}
				else if (startsWith(method_name, "enable")) {
					setter(method_name, makeStringView("Enabled"), args, attributes_ptr);
				}
				else {
					function(method_name, type, args, attributes_ptr);
				}
			}
		}		
		logError("'//@ end' not found while parsing component ", component_name);
	}

	void parseEvents() {
		StringView line;
		while (readLine(content, line)) {
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
		while (readLine(content, line)) {
			StringView word = consumeWord(line);
			if (equal(word, "virtual")) {
				StringView type = consumeType(line);
				StringView method_name = consumeIdentifier(line);
				StringView args = consumeArgs(line);
				StringView def = find(line, "//@");
				Attributes attributes;
				bool has_attributes = false;
				if (def.size() > 0) {
					def.begin += 3;
					has_attributes = parseAttributes(def, attributes);
				}
				function(method_name, type, args, has_attributes ? &attributes : nullptr);
			}
			else if (equal(word, "//@")) {
				word = consumeWord(line);
				if (equal(word, "end")) return;

				logError("Unexpected ", word);
			}
		}
	}
	
	void parseEnum() {
		StringView line;
		if (!readLine(content, line)) return;
		StringView word0 = consumeWord(line);
		if (!equal(word0, "enum")) {
			logError("Expected enum");
			return;
		}
		StringView enum_name = consumeWord(line);
		if (equal(enum_name, "class")) enum_name = consumeWord(line);

		Enum& e = current_module->enums.emplace(allocator);
		e.name = enum_name;
		last_enumerator_value = -1;

		for (;;) {
			if (!readLine(content, line)) {
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
			enumerator(enumerator_name, enumerator_value);
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
		while (readLine(content, line)) {
			if (!startsWith(line, "//@")) continue;
			line.begin += 3;
			line = skipWhitespaces(line);
			StringView word = consumeWord(line);
			if (equal(word, "functions")) {
				parseFunctions();
			}
			else if (equal(word, "enum")) {
				parseEnum();
			}
			else if (equal(word, "events")) {
				parseEvents();
			}
			else if (equal(word, "component")) {
				StringView cmp_name = consumeWord(line);
				StringView id = consumeWord(line);
				StringView label = consumeString(line);
				StringView def = consumeWord(line);
				StringView icon;

				if (def.size() > 0) {
					if (equal(def, "icon")) {
						icon = consumeWord(line);
					}
					else {
						logError("Unexpected ", def);
					}
				}
				parseComponent(cmp_name, id, label, icon);
			}
			else if (equal(word, "component_struct")) {
				StringView id = consumeWord(line);
				StringView label = consumeString(line);
				StringView icon;
				StringView def = consumeWord(line);
				StringView name;
				if (def.size() > 0) {
					if (equal(def, "icon")) {
						icon = consumeWord(line);
					}
					else if (equal(def, "name")) {
						name = consumeWord(line);
					}
					else {
						logError("Unexpected ", def);
					}
				}
				
				if (!readLine(content, line) || !equal(consumeWord(line), "struct")) {
					logError("Expected 'struct'");
					return;
				}
				StringView struct_name = consumeWord(line);
				if (struct_name.size() == 0) {
					logError("Expected struct name");
					return;
				}
				if (name.size() == 0) name = struct_name;
				
				parseComponentStruct(name, struct_name, id, label, icon);
			}
			else if (equal(word, "end")) {
				return;
			}
			else {
				logError("Unexpected \"", word, "\"");
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
		if (!readLine(content, line)) {
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

		while (readLine(content, line)) {
			line = skipWhitespaces(line);
			
			if (peekChar(line) == '}') break;

			StructVar& v = s.vars.emplace();
			v.type = consumeType(line);
			v.name = consumeIdentifier(line);
		}
	}

	void parse() {
		StringView line;
		while (readLine(content, line)) {
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
			else if (equal(word, "struct")) {
				parseStruct(line);
			}
			else {
				logError("Unexpected \"", word, "\"");
			}
		}		
	}

	void beginFile(StringView name) {
		filename = name;
	}

	void enumerator(StringView name, StringView value) {
		Enumerator& e = current_module->enums.last().values.emplace();
		e.name = name;
		if (value.size() > 0) {
			char tmp[64];
			buildString(tmp, value);
			e.value = atoi(tmp);
			last_enumerator_value = e.value;
		}
		else {
			++last_enumerator_value;
			e.value = last_enumerator_value;
		}
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

	void function(StringView name, StringView return_type, StringView args, const Attributes* attributes) {
		if (!current_component) {
			Function& fn = current_module->functions.emplace();
			fn.name = name;
			fn.args = args;
			fn.return_type = return_type;
			if (attributes) fn.attributes = *attributes;
			return;
		}

		Function& fn = current_component->functions.emplace();
		fn = Function();
		fn.name = name;
		fn.return_type = return_type;
		fn.args = args;
		if (attributes) fn.attributes = *attributes;
	}

	void setter(StringView method_name, StringView property_name, StringView args, const Attributes* attributes) {
		Property& prop = getProperty(property_name);
		if (attributes) prop.attributes = *attributes;
		prop.setter_name = method_name;
		prop.setter_args = args;
	}

	void getter(StringView return_type, StringView method_name, StringView property_name, StringView args, const Attributes* attributes) {
		Property& prop = getProperty(property_name);
		if (attributes) prop.attributes = *attributes;
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

	IAllocator& allocator;
	StringView filename;
	i32 last_enumerator_value = -1;
	Component* current_component = nullptr;
	Module* current_module = nullptr;
	ExpArray<Module> modules;
	ExpArray<Struct> structs;
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

void write(OutputStream& output, const Attributes& attributes) {
	if (attributes.is_radians) {
		output.add("\t\t\t.radiansAttribute()\n");
	}
	if (attributes.is_multiline) {
		output.add("\t\t\t.multilineAttribute()\n");
	}
	if (attributes.resource_type.size() > 0) {
		output.add("\t\t\t.resourceAttribute(", attributes.resource_type, ")\n");
	}
	if (attributes.is_color) {
		output.add("\t\t\t.colorAttribute()\n");
	}
	if (attributes.no_ui) {
		output.add("\t\t\t.noUIAttribute()\n");
	}
	if (attributes.min.size() > 0) {
		output.add("\t\t\t.minAttribute(", attributes.min, ")\n");
	}
	if (attributes.clamp_max.size() > 0) {
		output.add("\t\t\t.clampAttribute(", attributes.min, ", ", attributes.clamp_max, ")\n");
	}
}

StringView withoutNamespace(StringView ident) {
	StringView res = ident;
	res.begin = ident.end;
	while (res.begin != ident.begin && *res.begin != ':') --res.begin;
	if (*res.begin == ':') ++res.begin;
	return res;
}

bool isEnum(Module& m, StringView name) {
	for (const Enum& e : m.enums) {
		if (equal(e.name, name)) return true;
	}
	return false;
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
		word = withoutSuffix(word, 1);
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

void wrap(OutputStream& out, Module& m, Component& c, Function& f) {
	StringView label = f.attributes.label;
	if (label.size() == 0) label = f.name;
	out.add("int ",c.name,"_",label,"(lua_State* L) {\n");
	out.add("\tauto [imodule, entity] = checkComponent(L);\n");
	out.add("\tauto* module = (",m.name,"*)imodule;\n");
	
	i32 arg_idx = -1;
	StringView args = withoutPrefix(withoutSuffix(f.args, 1), 1);
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		++arg_idx;
		if (is_first) return; // skip entity, we alredy have it
		if (arg.is_const && equal(arg.type, "char*"))
			out.add("\tauto ",arg.name," = LuaWrapper::checkArg<const char*>(L, ",(arg_idx + 1),");\n");
		else
			out.add("\tauto ",arg.name," = LuaWrapper::checkArg<",arg.type,">(L, ",(arg_idx + 1),");\n");
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
		out.add(";\n");
		out.add("\tlua_newtable(L);\n");
		for (StructVar& v : st->vars) {
			out.add("\tLuaWrapper::push(L, s.",v.name,");\n");
			out.add("\tlua_setfield(L, -2, \"",v.name,"\");\n");
		}
		out.add("\n\treturn 1;\n");
	}
	else if (has_return) {
		out.add(");\n\treturn 1;\n");
	}
	else {
		out.add(";\n\treturn 0;\n");
	}
	out.add("}\n\n");
}

void wrap(OutputStream& out, StringView module, StringView component, StringView property_name, StringView method_name, StringView args, bool is_getter) {
	out.add("int ", (is_getter ? "get" : "set"), component, property_name, "(lua_State* L) {\n");
	i32 idx = 2;
	out.add("\tauto [imodule, entity] = checkComponent(L);\n");
	out.add("\tauto* module = (",module,"*)imodule;\n");
	
	args = withoutPrefix(withoutSuffix(args, 1), 1);
	forEachArg(args, [&](const Arg& arg, bool){
		out.add("\tauto ", arg.name, " = LuaWrapper::checkArg<",arg.type,">(L, ",idx,");\n");
		++idx;
	});
	if (is_getter) out.add("\tLuaWrapper::push(L, module->", method_name, "(");
	else out.add("\tmodule->", method_name, "(");
	forEachArg(args, [&](const Arg& arg, bool first){
		if (!first) out.add(", ");
		out.add(arg.name);
	});
	if (is_getter) out.add(")");
	out.add(");\n");
	if (is_getter) out.add("\treturn 1;\n");
	else out.add("\treturn 0;\n");
	out.add("}\n\n");
}

void serializeComponentRegister(OutputStream& out, Parser& parser) {
	out.add("namespace Lumix {\n");
	out.add("void registerLuaComponents(lua_State* L) {\n");
	for (Module& m : parser.modules) {
		for (Component& c : m.components) {
			out.add("\tregisterLuaComponent(L, \"",c.id,"\", ",c.name,"_getter, ",c.name,"_setter);\n");
		}
	}
	out.add("}\n");
	out.add("}\n\n");
}


void toID(StringView name, Span<char> out) {
	char* dst = out.begin;
	const char* src = name.begin;
	while (dst < out.end - 1 && src < name.end) {
		if (*src == ' ') {
			*dst = '_';
		}
		else if (*src >= 'A' && *src <= 'Z') {
			if (src != name.begin) {
				*dst = '_';
				++dst;
			}
			if (dst != out.end - 1) *dst = *src | 0x20;
		}
		else {
			*dst = *src;
		}
		++dst;
		++src;
	}
	*dst = 0;
}

StringView pickLabel(StringView base, StringView spec) {
	if (spec.size() > 0) return spec;
	return base;
}

void serializeLuaPropertySetter(OutputStream& out, Module& m, Component& c) {
	out.add("int ",c.name,"_setter(lua_State* L) {\n");
	out.add(R"#(
	auto [imodule, entity] = checkComponent(L);
	auto* module = ()#",m.name,R"#(*)imodule;
	const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
	if (false) {}
)#");

	// TODO
	bool is_array = false;
	for (Property& p : c.properties) {
		if (p.is_array_begin) {
			is_array = true;
			continue;
		}
		if (p.is_array_end) {
			is_array = false;
			continue;
		}
		if (is_array) continue;
		if (p.attributes.is_blob) continue;
		if (p.attributes.is_enum) continue;
		if (isEnum(m, p.type)) continue;
		if (p.attributes.force_function) continue;
		if (p.is_var) {
			out.add("\telse if (equalStrings(prop_name, \"",p.name,"\")) module->get",c.name,"(entity).",p.name," = LuaWrapper::checkArg<",p.type,">(L, 3);\n");
			continue;
		}

		if (p.getter_name.size() == 0) continue;
		if (p.setter_name.size() == 0) continue;

		char tmp[256];
		toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 255));
		out.add("\telse if (equalStrings(prop_name, \"",tmp,"\")) module->",p.setter_name,"(entity, LuaWrapper::checkArg<",p.type,">(L, 3));\n");
	}
	out.add("\telse { ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); }\n");

	out.add("\treturn 0;\n");
	out.add("}\n\n");
}

void serializeLuaArrayGetter(OutputStream& out, Module& m, Component& c, Property& p) {
	out.add("using GetterModule = ",m.name,";\n");
	out.add(
	R"#(			auto getter = [](lua_State* L) ->int {
		LuaWrapper::checkTableArg(L, 1); // self
		auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
		EntityRef entity{LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
		if (lua_type(L, 2) == LUA_TSTRING) {
			auto adder = [](lua_State* L) -> int  {
				auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
				EntityRef entity{LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
				module->add)#",p.name,R"#((entity, module->get)#",p.name,R"#(Count(entity));
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
			if (false) {}
)#");	
				bool is_sub = false;
				for (Property& sub : c.properties) {
					if (&sub == &p) {
						is_sub = true;
						continue;
					}
					else if (p.is_array_end) break;
					if (sub.attributes.is_blob) continue;
					if (sub.attributes.force_function) continue;
					if (sub.attributes.is_enum) continue;
					if (isEnum(m, sub.type)) continue;
					if (sub.getter_name.size() == 0) continue;
					if (is_sub) {
						char tmp[256];
						toID(pickLabel(sub.name, sub.attributes.label), Span(tmp, tmp + 256));
						out.add("\t\t\t\t\telse if(equalStrings(prop_name, \"",tmp,"\")) {\n");
						out.add("\t\t\t\t\t\tLuaWrapper::push(L, module->",sub.getter_name,"(entity, index));\n");
						out.add("\t\t\t\t\t}\n");
					}
				}
				out.add("\t\t\t\t\telse { ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); }\n");
out.add(R"#(
					return 1;
				};

				auto setter = [](lua_State* L) -> int {
					LuaWrapper::checkTableArg(L, 1);
					const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
					auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
					EntityRef entity = {LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
					i32 index = LuaWrapper::toType<int>(L, lua_upvalueindex(3));
					if (false) {}
)#");	
					
				is_sub = false;
				for (Property& sub : c.properties) {
					if (&sub == &p) {
						is_sub = true;
						continue;
					}
					else if (p.is_array_end) break;
					if (sub.attributes.is_blob) continue;
					if (sub.attributes.force_function) continue;
					if (sub.attributes.is_enum) continue;
					if (isEnum(m, sub.type)) continue;
					if (sub.setter_name.size() == 0) continue;
					if (is_sub) {
						char tmp[256];
						toID(pickLabel(sub.name, sub.attributes.label), Span(tmp, tmp + 256));
						out.add("\t\t\t\t\telse if(equalStrings(prop_name, \"",tmp,"\")) {\n");
						out.add("\t\t\t\t\t\tmodule->",sub.setter_name,"(entity, index, LuaWrapper::checkArg<",sub.type,">(L, 3));\n");
						out.add("\t\t\t\t\t}\n");
					}
				}
				out.add("\t\t\t\t\telse { ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); }\n");
out.add(R"#(
					return 0;
				};

				i32 index = LuaWrapper::checkArg<i32>(L, 2) - 1;
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
)#");	
}

void serializeLuaPropertyGetter(OutputStream& out, Module& m, Component& c) {
	out.add("int ",c.name,"_getter(lua_State* L) {\n");
	out.add("\tauto [imodule, entity] = checkComponent(L);\n");
	out.add("\tauto* module = (",m.name,"*)imodule;\n");

	if (equal(c.id, "lua_script")) {
		out.add(R"#(
			if (lua_isnumber(L, 2)) {
				const i32 scr_index = LuaWrapper::toType<i32>(L, 2) - 1;
				int env = module->getEnvironment(entity, scr_index);
				if (env < 0) {
					lua_pushnil(L);
				}
				else {
					lua_rawgeti(L, LUA_REGISTRYINDEX, env);
					ASSERT(lua_type(L, -1) == LUA_TTABLE);
				}
				return 1;
			}
		)#");
	}

	out.add(R"#(
	const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
	if (false) {}
)#");

	// TODO
	bool is_array = false;
	for (Property& p : c.properties) {
		if (p.is_array_begin) {
			out.add("\telse if (equalStrings(prop_name, \"",p.array_id,"\")) {\n");
			serializeLuaArrayGetter(out, m, c, p);
			out.add(R"#(
			lua_newtable(L); // {}
			lua_newtable(L); // {}, metatable
			LuaWrapper::push(L, module);
			LuaWrapper::push(L, entity.index);
			lua_pushcclosure(L, getter, "getter", 2);
			lua_setfield(L, -2, "__index"); // {}, mt
			lua_setmetatable(L, -2); // {}
			}
			)#");
			is_array = true;
			continue;
		}
		if (p.is_array_end) {
			is_array = false;
			continue;
		}
		if (is_array) continue;
		if (p.attributes.is_blob) continue;
		if (p.attributes.is_enum) continue;
		if (isEnum(m, p.type)) continue;
		if (p.attributes.force_function) continue;
		if (p.is_var) {
			out.add("\telse if (equalStrings(prop_name, \"",p.name,"\")) LuaWrapper::push(L, module->get",c.name,"(entity).",p.name,");\n");
			continue;
		}
		if (p.getter_name.size() == 0) continue;

		char tmp[256];
		toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 255));
		out.add("\telse if (equalStrings(prop_name, \"",tmp,"\")) LuaWrapper::push(L, module->",p.getter_name,"(entity));\n");
	}

	for (Function& f : c.functions) {
		StringView label = f.attributes.label;
		if (label.size() == 0) label = f.name;
		out.add("\telse if (equalStrings(prop_name, \"",label,"\")) {\n");
		out.add("\t\tlua_pushcfunction(L, ",c.name,"_",label,", \"",c.name,"_",f.name,"\");\n");
		out.add("\t}\n");
	}
	out.add("\telse { ASSERT(false); luaL_error(L, \"Unknown property %s\", prop_name); }\n");
	out.add("\treturn 1;\n");
	out.add("}\n\n");
}

void serializeLuaCAPI(OutputStream& out, Module& m) {
	out.add("namespace Lumix {\n");
	for (Component& c : m.components) {
		for (Function& f : c.functions) {
			wrap(out, m, c, f);
		}
		serializeLuaPropertyGetter(out, m, c);
		serializeLuaPropertySetter(out, m, c);
	}
	out.add("}\n\n");
}

void serializeReflection(OutputStream& out, Module& m) {
	out.add("// Generated by meta.cpp\n\n");
	for (Enum& e : m.enums) {
		out.add("struct ", e.name, "Enum : reflection::EnumAttribute {\n");
		out.add("\tu32 count(ComponentUID cmp) const override { return ",e.values.size,"; }\n");
		out.add("\tconst char* name(ComponentUID cmp, u32 idx) const override {\n");
		out.add("\t\tswitch((",e.name,")idx) {\n");
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
			out.add("\t\t\tcase ",e.name,"::",v.name,": return \"",tmp,"\";\n");
		}
		out.add("\t\t}\n");
		out.add("\t\tASSERT(false);\n");
		out.add("\t\treturn \"N/A\";\n");
		out.add("\t}\n");
		out.add("};\n\n");
	}

	out.add("reflection::build_module(\"", m.id, "\")\n");
	for (StringView e : m.events) {
		out.add("\t.event<&",m.name,"::",e,">(\"",e,"\")\n");
	}
	for (Function& fn : m.functions) {
		StringView label = fn.name;
		if (fn.attributes.label.size() > 0) label = fn.attributes.label;
		out.add("\t.function<(", fn.return_type, " (", m.name, "::*)", fn.args, ")&", m.name, "::", fn.name ,">(\"", label, "\")\n");
	}

	for (Component& cmp : m.components) {
		out.add("\t.cmp<&", m.name, "::create", cmp.name, ", &", m.name, "::destroy", cmp.name, ">(\"", cmp.id, "\", \"", m.label, " / ", cmp.label, "\")\n");
		StringView array;
		
		if (cmp.icon.size() > 0) {
			out.add("\t\t.icon(", cmp.icon, ")\n");
		}

		for (Function& fn : cmp.functions) {
			StringView label = fn.attributes.label.size() > 0 ? fn.attributes.label : fn.name;
			out.add("\t\t.function<(", fn.return_type, " (", m.name, "::*)", fn.args, ")&", m.name, "::", fn.name, ">(\"", label, "\")\n");
		}

		for (Property& prop : cmp.properties) {
			if (prop.is_array_begin) {
				out.add("\t\t.begin_array<&", m.name, "::get", prop.name, "Count, &", m.name, "::add", prop.name, ", &", m.name, "::remove", prop.name, ">(\"", prop.array_id ,"\")\n");
				array = prop.name;
			}
			else if (prop.is_array_end) {
				out.add("\t\t.end_array()\n");
				array = {};
			}
			else if (prop.is_var) {
				char label[256];
				if (prop.attributes.label.size() > 0) {
					buildString(label, prop.attributes.label);
				}
				else {
					toLabel(prop.name, Span(label, label + sizeof(label)));
				}
				out.add("\t\t.var_prop<&", m.name, "::get", cmp.name, ", &", cmp.struct_name, "::", prop.name, ">(\"", label, "\")");
				out.add("\n");
				write(out, prop.attributes);
			}
			else if (prop.getter_name.size() > 0) {
				bool is_enum = isEnum(m, prop.type) || prop.attributes.is_enum || prop.attributes.dynamic_enum_name.size() > 0;
				if (equal(prop.name, "Enabled")) {
					out.add("\t\t.prop<&", m.name, "::", prop.getter_name);
					if (prop.setter_name.size() > 0) {
						out.add(", &", m.name, "::", prop.setter_name);
					}
				}
				else {
					if (prop.attributes.is_blob) {
						out.add("\t\t.blob_property<&", m.name, "::", prop.getter_name);
					}
					else if (is_enum) {
						out.add("\t\t.enum_prop<&", m.name, "::", prop.getter_name);
					}
					else {
						out.add("\t\t.prop<&", m.name, "::", prop.getter_name);
					}
					if (prop.setter_name.size() > 0) {
						out.add(", &", m.name, "::", prop.setter_name);
					}
				}
				char label[256];
				if (prop.attributes.label.size() > 0) {
					buildString(label, prop.attributes.label);
				}
				else {
					toLabel(prop.name, Span(label, label + sizeof(label)));
				}
				out.add(">(\"", label, "\")\n");
				write(out, prop.attributes);
				if (is_enum) {
					// TODO withoutNamespace?
					StringView enum_name = prop.attributes.dynamic_enum_name.size() > 0 ? prop.attributes.dynamic_enum_name : withoutNamespace(prop.type);
					out.add("\t\t\t.attribute<",enum_name,"Enum>()\n");
				}
			}
			else if (prop.setter_name.size() > 0) {
				out.add("\t\t.function<&", m.name, "::",  prop.setter_name, ">(\"set", prop.name, "\")\n");
			}
		}
	}
	out.add(";\n\n");
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
	lua_capi_stream.add("// Generated by meta.cpp\n\n");
	for (Module& m : parser.modules) {
		StringView include_path = withoutPrefix(makeStringView(m.filename), 2); // skip "./"
		lua_capi_stream.add("#include \"",include_path,"\"\n");
	}
	lua_capi_stream.add("\n");

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
	serializeComponentRegister(lua_capi_stream, parser);

	writeFile("lua/lua_capi.gen.h", lua_capi_stream);

	QueryPerformanceCounter(&stop);
	QueryPerformanceFrequency(&freq);
	i32 duration = i32(float((stop.QuadPart - start.QuadPart) / double(freq.QuadPart)) * 1000);
	char tmp[32];
	_itoa_s(duration, tmp, 10);
	logInfo("Processed in ", tmp, " ms");
	return 0;
}