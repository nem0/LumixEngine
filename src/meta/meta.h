#pragma once

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

using i32 = int;

struct NewPlaceholder {};
inline void* operator new(size_t, NewPlaceholder, void* where) { return where; }
inline void operator delete(void*, NewPlaceholder,  void*) { } 

struct StringView {
	const char* begin = nullptr;
	const char* end = nullptr;

	i32 size() const { return i32(end - begin); }
	char operator[] (i32 index) { return begin[index]; }
};

template <typename T>
struct Span {
	T* begin;
	T* end;
};

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


struct Attributes {
	StringView alias;
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

struct StructVar {
	StringView type;
	StringView name;
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

struct MetaData {
	ExpArray<Module>& modules;
	ExpArray<Struct>& structs;
	ExpArray<Object>& objects;
	ExpArray<Enum>& enums;
};

struct MetaPluginRegister {
	using Fn = void (*)(MetaData&);
	MetaPluginRegister(Fn fn)
		: fn(fn)
	{
		next = first;
		first = this;
	}

	Fn fn;
	static inline MetaPluginRegister* first = nullptr;
	MetaPluginRegister* next = nullptr;
};

struct OutputStream {
	OutputStream();
	~OutputStream();

	void consume(OutputStream& rhs);

	template <typename... Args>
	void add(Args... args) {
		(append(args), ...);
	}

	void append(const char* v);
	void append(XXH64_hash_t hash);
	void append(i32 value);
	void reserve(i32 size);
	void append(StringView v);

	char* data;
	i32 capacity = 64 * 1024;
	i32 length = 0;
};

void writeFile(const char* out_path, OutputStream& stream);
void formatCPP(OutputStream& out);
StringView makeStringView(const char* str);
bool startsWith(StringView str, StringView prefix);
bool startsWith(StringView str, const char* prefix);
StringView withoutPrefix(StringView str, i32 prefix_len);
StringView find(StringView haystack, StringView needle);
StringView find(StringView haystack, const char* needle);
bool isBlob(const Property& p);
bool equal(StringView lhs, StringView rhs);
bool equal(StringView lhs, const char* rhs);

#define META_PLUGIN(f) static MetaPluginRegister reg(f);