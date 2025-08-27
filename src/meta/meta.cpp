#include "core/defer.h"
#include <float.h>
#include <Windows.h>

using i32 = int;

struct StringView {
	const char* begin = nullptr;
	const char* end = nullptr;

	i32 size() { return i32(end - begin); }
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
	return isspace(c) || c == '(';
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

StringView consumeType(StringView& str) {
	return consumeWord(str);
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
	StringView min;
	StringView max;
	bool is_radians = false;
	bool is_color = false;
};

struct IParseVisitor {
	virtual void beginModule(StringView name, StringView id, StringView label) = 0;
	virtual void endModule() = 0;
	virtual void beginComponent(StringView name, StringView id, StringView label) = 0;
	virtual void endComponent() = 0;
	virtual void beginArray(StringView name) = 0;
	virtual void endArray() = 0;
	virtual void function(StringView name) = 0;
	virtual void setter(StringView property_name, StringView args) = 0;
	virtual void getter(StringView property_name, StringView args) = 0;
	virtual void variable(StringView type, StringView property_name, const Attributes& attributes) = 0;
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

	void parseSetter(StringView component_name, StringView name, StringView args) {
		if (!startsWith(name, component_name)) {
			logError("Expected ", component_name);
			return;
		}
		name.begin += component_name.size();
		
		visitor->setter(name, args);
	}

	void parseGetter(StringView component_name, StringView name, StringView args) {
		if (!startsWith(name, component_name)) {
			logError("Expected ", component_name);
			return;
		}
		name.begin += component_name.size();
		visitor->getter(name, args);
	}

	void propertyVariable(StringView line, StringView def) {
		line = skipWhitespaces(line);
		StringView type = consumeType(line);
		StringView name = consumeIdentifier(line);
		if (name.end != name.begin && *(name.end - 1) == ';') --name.end; // trim ';'
		
		Attributes attributes;
		StringView word = consumeWord(def);
		while (word.size() > 0) {
			if (equal(word, "radians")) {
				attributes.is_radians = true;
			}
			else if (equal(word, "color")) {
				attributes.is_color = true;
			}
			else if (equal(word, "min")) {
				attributes.min = consumeWord(def);
			}
			else if (equal(word, "max")) {
				attributes.max = consumeWord(def);
			}
			else {
				logError("Unknown attribute ", word);
			}
			word = consumeWord(def);
		}
		
		visitor->variable(type, name, attributes);
	}

	void parseComponentStruct(StringView id, StringView label) {
		StringView line;
		// TODO errors
		if (!readLine(content, line)) return;
		if (!equal(consumeWord(line), "struct")) return;
		StringView name = consumeWord(line);

		visitor->beginComponent(name, id, label);
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
		logError("'//@ end' not found while parsing component ", name);
	}

	void parseArray(StringView component_name, StringView array_name) {
		StringView line;
		visitor->beginArray(array_name);
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

				if (startsWith(method_name, "get")) {
					method_name.begin += 3;
					if (!startsWith(method_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						method_name.begin += array_name.size();
						if (!equal(method_name, "Count")) {
							visitor->getter(method_name, args);
						}
					}
				}
				else if (startsWith(method_name, "set")) {
					method_name.begin += 3;
					if (!startsWith(method_name, array_name)) {
						logError("Expected ", array_name);
					}
					else {
						method_name.begin += array_name.size();
						visitor->setter(method_name, args);
					}
				}
			}
		}
		logError("'//@ end' not found while parsing ", component_name, ".", array_name);
	}

	void parseComponent(StringView component_name, StringView id, StringView label) {
		visitor->beginComponent(component_name, id, label);
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
					parseArray(component_name, array_name);
				}
				else {
					logError("Unexpected \"", word, "\"");
				}
			}
			else if (equal(word, "virtual")) {
				StringView type = consumeType(line);
				StringView method_name = consumeIdentifier(line);
				StringView args = consumeArgs(line);

				if (startsWith(method_name, "set")) {
					method_name.begin += 3;
					parseSetter(component_name, method_name, args);
				}
				else if (startsWith(method_name, "get")) {
					method_name.begin += 3;
					parseGetter(component_name, method_name, args);
				}
				else if (startsWith(method_name, "is")) {
					method_name.begin += 2;
					parseGetter(component_name, method_name, args);
				}
				else if (startsWith(method_name, "enable")) {
					visitor->setter(makeStringView("Enabled"), args);
				}
				else {
					visitor->function(method_name);
				}
			}
		}		
		logError("'//@ end' not found while parsing component ", component_name);
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
			if (equal(word, "component")) {
				StringView cmp_name = consumeWord(line);
				StringView id = consumeWord(line);
				StringView label = consumeString(line);
				parseComponent(cmp_name, id, label);
			}
			else if (equal(word, "component_struct")) {
				StringView id = consumeWord(line);
				StringView label = consumeString(line);
				parseComponentStruct(id, label);
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
	bool has_setter  = false;
	bool has_getter  = false;
	bool is_var = false;
	bool is_array_begin = false;
	bool is_array_end = false;
	Attributes attributes;
};

struct Function {
	StringView name;
};

struct Component {
	StringView name;
	StringView id;
	StringView label;
	Function functions[32];
	i32 num_functions = 0;
	Property properties[1024];
	i32 num_properties = 0;
	Component* next = nullptr;
};

struct Reflector : IParseVisitor {
	void beginModule(StringView name, StringView id, StringView label) override {
		output.add("reflection::build_module(\"", id, "\")\n");
		current_module = name;
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

	void endModule() override {
		Component* cmp = components;
		while (cmp) {
			output.add("\t.cmp<&", current_module, "::create", cmp->name, ", &", current_module, "::destroy", cmp->name, ">(\"", cmp->id, "\", \"", current_module_label, " / ", cmp->label, "\")\n");
			StringView array;
			
			for (i32 i = 0; i < cmp->num_functions; ++i) {
				Function& fn = cmp->functions[i];
				output.add("\t\t.function<&", current_module, "::", fn.name, ">(\"", fn.name, "\", \"", fn.name, "\")\n");
			}

			for (i32 i = 0; i < cmp->num_properties; ++i) {
				Property& prop = cmp->properties[i];
				if (prop.is_array_begin) {
					output.add("\t\t.begin_array<&", current_module, "::get", prop.name, "Count, &", current_module, "::add", prop.name, ", &", current_module, "::remove", prop.name, ">(\"", prop.name ,"\")\n");
					array = prop.name;
				}
				else if (prop.is_array_end) {
					output.add("\t\t.end_array()\n");
					array = {};
				}
				else if (prop.is_var) {
					char label[256];
					toLabel(prop.name, Span(label, label + sizeof(label)));
					output.add("\t\t.var_prop<&", current_module, "::get", cmp->name, ", &", cmp->name, "::", prop.name, ">(\"", label, "\")");
					if (prop.attributes.is_radians) {
						output.add(".radiansAttribute()");
					}
					if (prop.attributes.is_color) {
						output.add(".colorAttribute()");
					}
					if (prop.attributes.min.size() > 0) {
						output.add(".minAttribute(", prop.attributes.min, ")");
					}
					if (prop.attributes.max.size() > 0) {
						output.add(".maxAttribute(", prop.attributes.max, ")");
					}
					output.add("\n");
				}
				else if (prop.has_getter) {
					StringView object_name = cmp->name;
					if (array.size() > 0) object_name = array;
					if (equal(prop.name, "Enabled")) {
						output.add("\t\t.prop<&", current_module, "::is", object_name, prop.name);
						if (prop.has_getter && prop.has_setter) {
							output.add(", &", current_module, "::enable", object_name);
						}
					}
					else {
						output.add("\t\t.prop<&", current_module, "::get", object_name, prop.name);
						if (prop.has_getter && prop.has_setter) {
							output.add(", &", current_module, "::set", object_name, prop.name);
						}
					}
					char label[256];
					toLabel(prop.name, Span(label, label + sizeof(label)));
					output.add(">(\"", label, "\")\n");
				}
				else if (prop.has_setter) {
					output.add("\t\t.function<&", current_module, "::set", cmp->name, prop.name, ">(\"set", prop.name, "\", \"set", cmp->name, prop.name, "\")\n");
				}
			}
			Component* next = cmp->next;
			delete cmp;
			cmp = next;
		}
		output.add(";\n\n");
	}

	void beginArray(StringView name) override {
		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;
		cmp->properties[cmp->num_properties] = Property();
		cmp->properties[cmp->num_properties].name = name;
		cmp->properties[cmp->num_properties].is_array_begin = true;
		++cmp->num_properties;
	}

	void endArray() override {
		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;
		cmp->properties[cmp->num_properties] = Property();
		cmp->properties[cmp->num_properties].is_array_end = true;
		++cmp->num_properties;
	}

	void beginComponent(StringView name, StringView id, StringView label) override {
		current_component = id;
		
		Component* comp = components;
		while (comp && !equal(comp->id, current_component)) {
			comp = comp->next;
		}
		if (!comp) {
			comp = new Component;
			comp->id = id;
			comp->name = name;
			comp->next = components;
			comp->label = label;
			components = comp;
		}
		
	}

	void endComponent() override {
		current_component = {};
	}
	
	void function(StringView name) override {
		Component* cmp = components;
		while (cmp && !equal(cmp->id, current_component)) cmp = cmp->next;

		cmp->functions[cmp->num_functions] = Function();
		cmp->functions[cmp->num_functions].name = name;
		++cmp->num_functions;
	}

	void setter(StringView property_name, StringView args) override {
		Property& prop = getProperty(property_name);
		prop.has_setter = true;
	}

	void getter(StringView property_name, StringView args) override {
		Property& prop = getProperty(property_name);
		prop.has_getter = true;
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
	StringView current_component;
	StringView current_module;
	StringView current_module_label;
	Component* components = nullptr;
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
	Parser parser;
	parser.visitor = &visitor;
	parser.filename = filename;
	parser.content.begin = data;
	parser.content.end = data + size;
	parser.parse();
	
	visitor.output.data[visitor.output.length] = 0;
	if (visitor.output.length > 0) {
		OutputDebugString(visitor.output.data);
		// write reflection output beside source file as *.inl.h
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