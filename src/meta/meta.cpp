#include "core/defer.h"
#include <float.h>
#include <Windows.h>

using i32 = int;

template <typename T, i32 CAPACITY>
struct InplaceArray {
	T& push() { ++size; return values[size - 1]; }
	T values[CAPACITY];
	i32 size = 0;

	T* begin() { return values; }
	T* end() { return values + size; }
	const T* begin() const { return values; }
	const T* end() const { return values + size; }
	T& last() { return values[size - 1]; }
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

struct IParseVisitor {
	virtual void beginModule(StringView name, StringView id, StringView label) = 0;
	virtual void endModule() = 0;
	virtual void beginComponent(StringView name, StringView struct_name, StringView id, StringView label, StringView icon) = 0;
	virtual void endComponent() = 0;
	virtual void beginArray(StringView name, StringView array_id) = 0;
	virtual void endArray() = 0;
	virtual void beginEnum(StringView name) = 0;
	virtual void enumerator(StringView name, StringView value) = 0;
	virtual void function(StringView name, StringView return_type, StringView args, const Attributes* attributes) = 0;
	virtual void setter(StringView method_name, StringView property_name, StringView args, const Attributes* attributes) = 0;
	virtual void getter(StringView return_type, StringView method_name, StringView property_name, StringView args, const Attributes* attributes) = 0;
	virtual void variable(StringView type, StringView property_name, const Attributes& attributes) = 0;
	virtual void event(StringView type, StringView name) = 0;
};

struct Parser {
	StringView filename;
	StringView content;
	IParseVisitor* visitor;
	i32 line_idx = 0;

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
		visitor->getter(return_type, method_name, name, args, attributes);
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
		
		visitor->variable(type, name, attributes);
	}

	void parseComponentStruct(StringView name, StringView struct_name, StringView id, StringView label, StringView icon) {
		StringView line;
		visitor->beginComponent(name, struct_name, id, label, icon);
		defer { visitor->endComponent(); };

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
		visitor->beginArray(array_name, array_id);
		defer { visitor->endArray(); };
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
							visitor->getter(type, method_name, property_name, args, attributes_ptr);
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
							visitor->getter(type, method_name, property_name, args, attributes_ptr);
						}
					}
				}
				else if (startsWith(method_name, "set")) {
					property_name.begin += 3;
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						visitor->setter(method_name, property_name, args, attributes_ptr);
					}
				}
				else if (startsWith(method_name, "enable")) {
					property_name.begin += 6;
					if (!startsWith(property_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						visitor->setter(method_name, property_name, args, attributes_ptr);
					}
				}
			}
		}
		logError("'//@ end' not found while parsing ", component_name, ".", array_name);
	}

	void parseComponent(StringView component_name, StringView id, StringView label, StringView icon) {
		visitor->beginComponent(component_name, {}, id, label, icon);
		defer { visitor->endComponent(); };

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
					visitor->function(method_name, type, args, attributes_ptr);
				}
				else if (attributes.force_setter) {
					visitor->setter(method_name, attributes.property_name, args, attributes_ptr);
				}
				else if (attributes.force_getter) {
					visitor->getter(type, method_name, attributes.property_name, args, attributes_ptr);
				}
				else if (startsWith(method_name, "set")) {
					property_name.begin += 3;
					if (!startsWith(property_name, component_name)) {
						logError("Expected ", component_name);
						continue;
					}
					property_name.begin += component_name.size();
					
					visitor->setter(method_name, property_name, args, attributes_ptr);
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
					visitor->setter(method_name, makeStringView("Enabled"), args, attributes_ptr);
				}
				else {
					visitor->function(method_name, type, args, attributes_ptr);
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
				visitor->event(type, method_name);
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
				visitor->function(method_name, type, args, has_attributes ? &attributes : nullptr);
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
		visitor->beginEnum(enum_name);

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
			visitor->enumerator(enumerator_name, enumerator_value);
		}
	}

	void parseModule(StringView module_name, StringView id, StringView label) {
		visitor->beginModule(module_name, id, label);
		defer { visitor->endModule(); };
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
			else {
				logError("Unexpected \"", word, "\"");
			}
		}		
	}
};


struct Property {
	StringView name;
	StringView type;
	StringView getter_name;
	StringView setter_name;
	StringView array_id;
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
	StringView name;
	StringView struct_name;
	StringView id;
	StringView label;
	StringView icon;
	Function functions[32];
	i32 num_functions = 0;
	Property properties[1024];
	i32 num_properties = 0;
	Component* next = nullptr;
};

struct Enumerator {
	StringView name;
	i32 value;
};

struct Enum {
	StringView name;
	InplaceArray<Enumerator, 64> values;
};

struct Reflector : IParseVisitor {
	void beginModule(StringView name, StringView id, StringView label) override {
		has_any_export = true;
		num_module_functions = 0;
		current_module = name;
		current_module_id = id;
		current_module_label = label;
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

	void outputAttributes(const Attributes& attributes) {
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

	bool isEnum(StringView name) {
		for (const Enum& e : enums) {
			if (equal(e.name, name)) return true;
		}
		return false;
	}

	StringView withoutNamespace(StringView ident) {
		StringView res = ident;
		res.begin = ident.end;
		while (res.begin != ident.begin && *res.begin != ':') --res.begin;
		if (*res.begin == ':') ++res.begin;
		return res;
	}

	void endModule() override {
		for (const Enum& e : enums) {
			output.add("struct ", e.name, "Enum : reflection::EnumAttribute {\n");
			output.add("\tu32 count(ComponentUID cmp) const override { return ",e.values.size,"; }\n");
			output.add("\tconst char* name(ComponentUID cmp, u32 idx) const override {\n");
			output.add("\t\tswitch((",e.name,")idx) {\n");
			for (const Enumerator& v : e.values) {
				output.add("\t\t\tcase ",e.name,"::",v.name,": return \"",v.name,"\";\n");
			}
			output.add("\t\t}\n");
			output.add("\t\tASSERT(false);\n");
			output.add("\t\treturn \"N/A\";\n");
			output.add("\t}\n");
			output.add("};\n\n");
		}

		output.add("reflection::build_module(\"", current_module_id, "\")\n");
		
		for (StringView e : events) {
			output.add("\t.event<&",current_module,"::",e,">(\"",e,"\")\n");
		}

		for (i32 i = 0; i < num_module_functions; ++i) {
			Function& fn = module_functions[i];
			StringView label = fn.name;
			if (fn.attributes.label.size() > 0) label = fn.attributes.label;
			output.add("\t.function<(", fn.return_type, " (", current_module, "::*)", fn.args, ")&", current_module, "::", fn.name ,">(\"", label, "\", \"", current_module, "::", fn.name ,"\")\n");
		}
		

		Component* cmp = components;
		while (cmp) {
			output.add("\t.cmp<&", current_module, "::create", cmp->name, ", &", current_module, "::destroy", cmp->name, ">(\"", cmp->id, "\", \"", current_module_label, " / ", cmp->label, "\")\n");
			StringView array;
			
			if (cmp->icon.size() > 0) {
				output.add("\t\t.icon(", cmp->icon, ")\n");
			}

			for (i32 i = 0; i < cmp->num_functions; ++i) {
				Function& fn = cmp->functions[i];
				StringView label = fn.attributes.label.size() > 0 ? fn.attributes.label : fn.name;
				output.add("\t\t.function<(", fn.return_type, " (", current_module, "::*)", fn.args, ")&", current_module, "::", fn.name, ">(\"", label, "\", \"", current_module, "::", fn.name, "\")\n");
			}

			for (i32 i = 0; i < cmp->num_properties; ++i) {
				Property& prop = cmp->properties[i];
				if (prop.is_array_begin) {
					output.add("\t\t.begin_array<&", current_module, "::get", prop.name, "Count, &", current_module, "::add", prop.name, ", &", current_module, "::remove", prop.name, ">(\"", prop.array_id ,"\")\n");
					array = prop.name;
				}
				else if (prop.is_array_end) {
					output.add("\t\t.end_array()\n");
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
					output.add("\t\t.var_prop<&", current_module, "::get", cmp->name, ", &", cmp->struct_name, "::", prop.name, ">(\"", label, "\")");
					output.add("\n");
					outputAttributes(prop.attributes);
				}
				else if (prop.getter_name.size() > 0) {
					bool is_enum = isEnum(prop.type) || prop.attributes.is_enum || prop.attributes.dynamic_enum_name.size() > 0;
					if (equal(prop.name, "Enabled")) {
						output.add("\t\t.prop<&", current_module, "::", prop.getter_name);
						if (prop.setter_name.size() > 0) {
							output.add(", &", current_module, "::", prop.setter_name);
						}
					}
					else {
						if (prop.attributes.is_blob) {
							output.add("\t\t.blob_property<&", current_module, "::", prop.getter_name);
						}
						else if (is_enum) {
							output.add("\t\t.enum_prop<&", current_module, "::", prop.getter_name);
						}
						else {
							output.add("\t\t.prop<&", current_module, "::", prop.getter_name);
						}
						if (prop.setter_name.size() > 0) {
							output.add(", &", current_module, "::", prop.setter_name);
						}
					}
					char label[256];
					if (prop.attributes.label.size() > 0) {
						buildString(label, prop.attributes.label);
					}
					else {
						toLabel(prop.name, Span(label, label + sizeof(label)));
					}
					output.add(">(\"", label, "\")\n");
					outputAttributes(prop.attributes);
					if (is_enum) {
						// TODO withoutNamespace?
						StringView enum_name = prop.attributes.dynamic_enum_name.size() > 0 ? prop.attributes.dynamic_enum_name : withoutNamespace(prop.type);
						output.add("\t\t\t.attribute<",enum_name,"Enum>()\n");
					}
				}
				else if (prop.setter_name.size() > 0) {
					output.add("\t\t.function<&", current_module, "::",  prop.setter_name, ">(\"set", prop.name, "\", \"", current_module, "::", prop.setter_name, "\")\n");
				}
			}
			Component* next = cmp->next;
			delete cmp;
			cmp = next;
		}
		output.add(";\n\n");
	}

	void beginEnum(StringView name) override {
		Enum& e = enums.push();
		e.name = name;
		last_enumerator_value = -1;
	}

	void enumerator(StringView name, StringView value) override {
		Enumerator& e = enums.last().values.push();
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

	void beginArray(StringView name, StringView array_id) override {
		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;
		cmp->properties[cmp->num_properties] = Property();
		cmp->properties[cmp->num_properties].name = name;
		cmp->properties[cmp->num_properties].is_array_begin = true;
		cmp->properties[cmp->num_properties].array_id = array_id;
		++cmp->num_properties;
	}

	void endArray() override {
		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;
		cmp->properties[cmp->num_properties] = Property();
		cmp->properties[cmp->num_properties].is_array_end = true;
		++cmp->num_properties;
	}

	void beginComponent(StringView name, StringView struct_name, StringView id, StringView label, StringView icon) override {
		current_component = id;

		Component* comp = components;
		while (comp && !equal(comp->id, current_component)) {
			comp = comp->next;
		}
		if (!comp) {
			comp = new Component;
			comp->id = id;
			comp->name = name;
			comp->struct_name = struct_name;
			comp->next = components;
			comp->icon = icon;
			comp->label = label;
			components = comp;
		}
		
	}

	void endComponent() override {
		current_component = {};
	}
	
	void function(StringView name, StringView return_type, StringView args, const Attributes* attributes) override {
		if (current_component.size() == 0) {
			Function& fn = module_functions[num_module_functions];
			fn.name = name;
			fn.args = args;
			fn.return_type = return_type;
			if (attributes) fn.attributes = *attributes;
			++num_module_functions;
			return;
		}

		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;

		Function& fn = cmp->functions[cmp->num_functions];
		fn = Function();
		fn.name = name;
		fn.return_type = return_type;
		fn.args = args;
		if (attributes) cmp->functions[cmp->num_functions].attributes = *attributes;
		++cmp->num_functions;
	}

	void setter(StringView method_name, StringView property_name, StringView args, const Attributes* attributes) override {
		Property& prop = getProperty(property_name);
		if (attributes) prop.attributes = *attributes;
		prop.setter_name = method_name;
	}

	void getter(StringView return_type, StringView method_name, StringView property_name, StringView args, const Attributes* attributes) override {
		Property& prop = getProperty(property_name);
		if (attributes) prop.attributes = *attributes;
		prop.getter_name = method_name;
		prop.type = return_type;
	}
	
	void event(StringView type, StringView name) override {
		events.push() = name;
	}

	void variable(StringView type, StringView property_name, const Attributes& attributes) override {
		Property& prop = getProperty(property_name);
		prop.is_var = true;
		prop.attributes = attributes;
	}

	Property& getProperty(StringView name) {
		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;

		for (i32 i = 0; i < cmp->num_properties; ++i) {
			if (equal(cmp->properties[i].name, name)) return cmp->properties[i];
		}
		cmp->properties[cmp->num_properties] = Property();
		cmp->properties[cmp->num_properties].name = name;
		++cmp->num_properties;
		return cmp->properties[cmp->num_properties - 1];
	}

	OutputStream output;
	InplaceArray<Enum, 64> enums;
	InplaceArray<StringView, 64> events;
	i32 last_enumerator_value = -1;
	Function module_functions[64];
	i32 num_module_functions = 0;
	StringView current_component;
	StringView current_module;
	StringView current_module_label;
	StringView current_module_id;
	Component* components = nullptr;
	bool has_any_export = false;
};

void parseFile(StringView path, StringView filename) {
	char full[MAX_PATH];
	buildString(full, path, "/", filename);

	HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;
	defer { CloseHandle(h); };

	DWORD size = GetFileSize(h, nullptr);
	if (size == INVALID_FILE_SIZE) return;

	char* data = new char[size + 1];
	defer { delete[] data; };
	DWORD read = 0;
	if (ReadFile(h, data, size, &read, nullptr)) {
		data[read] = 0;
	}

	Reflector visitor;
	visitor.output.add("// generated by meta.cpp\n\n");
	Parser parser;
	parser.visitor = &visitor;
	parser.filename = filename;
	parser.content.begin = data;
	parser.content.end = data + size;
	parser.parse();
	
	visitor.output.data[visitor.output.length] = 0;
	if (visitor.has_any_export) {
		// write reflection output beside source file as *.gen.h
		char out_path[MAX_PATH];
		{
			const char* b = filename.begin;
			const char* e = filename.end;
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
			buildString(out_path, path, "/", stem, ".gen.h");
		}
		// skip writing if file exists and content is identical
		{
			bool unchanged = false;
			HANDLE h_existing = CreateFileA(out_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h_existing != INVALID_HANDLE_VALUE) {
				DWORD existing_size = GetFileSize(h_existing, nullptr);
				if (existing_size == (DWORD)visitor.output.length) {
					char* existing_data = new char[existing_size];
					DWORD read_bytes = 0;
					if (ReadFile(h_existing, existing_data, existing_size, &read_bytes, nullptr) && read_bytes == existing_size) {
						if (memcmp(existing_data, visitor.output.data, existing_size) == 0) {
							unchanged = true;
						}
					}
					delete[] existing_data;
				}
				CloseHandle(h_existing);
			}
			if (unchanged) return;
		}
		
		HANDLE hout = CreateFileA(out_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hout != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(hout, visitor.output.data, (DWORD)visitor.output.length, &written, nullptr);
			CloseHandle(hout);
		}

	}
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

int main() {
	scan(makeStringView("."));
	return 0;
}