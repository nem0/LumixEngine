#pragma once


#include "engine/lumix.h"
#include "engine/default_allocator.h"


namespace Lumix
{


LUMIX_ENGINE_API const char* stristr(const char* haystack, const char* needle);
LUMIX_ENGINE_API bool toCStringHex(u8 value, char* output, int length);
LUMIX_ENGINE_API bool toCStringPretty(i32 value, char* output, int length);
LUMIX_ENGINE_API bool toCStringPretty(u32 value, char* output, int length);
LUMIX_ENGINE_API bool toCStringPretty(u64 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(i32 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(i64 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(u64 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(u32 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(float value, char* output, int length, int after_point);
LUMIX_ENGINE_API const char* reverseFind(const char* begin_haystack, const char* end_haystack, char c);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, i32* value);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, i64* value);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, u32* value);
LUMIX_ENGINE_API bool copyString(char* destination, int length, const char* source);
LUMIX_ENGINE_API bool copyNString(char* destination,
	int length,
	const char* source,
	int source_len);
LUMIX_ENGINE_API bool catString(char* destination, int length, const char* source);
LUMIX_ENGINE_API bool catNString(char* destination, int length, const char* source, int source_len);
LUMIX_ENGINE_API bool makeLowercase(char* destination, int length, const char* source);
LUMIX_ENGINE_API char* trimmed(char* str);
LUMIX_ENGINE_API bool startsWith(const char* str, const char* prefix);
LUMIX_ENGINE_API int stringLength(const char* str);
LUMIX_ENGINE_API bool equalStrings(const char* lhs, const char* rhs);
LUMIX_ENGINE_API int compareMemory(const void* lhs, const void* rhs, size_t size);
LUMIX_ENGINE_API int compareString(const char* lhs, const char* rhs);
LUMIX_ENGINE_API int compareStringN(const char* lhs, const char* rhs, int length);
LUMIX_ENGINE_API int compareIStringN(const char* lhs, const char* rhs, int length);
LUMIX_ENGINE_API void copyMemory(void* dest, const void* src, size_t count);
LUMIX_ENGINE_API void moveMemory(void* dest, const void* src, size_t count);
LUMIX_ENGINE_API void setMemory(void* ptr, u8 value, size_t num);
LUMIX_ENGINE_API const char* findSubstring(const char* str, const char* substr);
LUMIX_ENGINE_API bool endsWith(const char* str, const char* substr);


inline bool isLetter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}


inline bool isUpperCase(char c)
{
	return c >= 'A' && c <= 'Z';
}


template <int SIZE> bool copyString(char(&destination)[SIZE], const char* source)
{
	return copyString(destination, SIZE, source);
}

template <int SIZE> bool catString(char(&destination)[SIZE], const char* source)
{
	return catString(destination, SIZE, source);
}


template <int size> struct StaticString
{
	StaticString() { data[0] = '\0'; }

	explicit StaticString(const char* str) { Lumix::copyString(data, size, str); }

	template <typename... Args> StaticString(const char* str, Args... args)
	{
		Lumix::copyString(data, size, str);
		int tmp[] = {(add(args), 0)...};
		(void)tmp;
	}

	template <int value_size> StaticString& operator<<(StaticString<value_size>& value)
	{
		add(value);
		return *this;
	}

	template <typename T> StaticString& operator<<(T value)
	{
		add(value);
		return *this;
	}

	template <int value_size> void add(StaticString<value_size>& value) { Lumix::catString(data, size, value.data); }
	void add(const char* value) { Lumix::catString(data, size, value); }
	void add(char* value) { Lumix::catString(data, size, value); }

	void operator=(const char* str) { copyString(data, str); }

	void add(float value)
	{
		int len = Lumix::stringLength(data);
		Lumix::toCString(value, data + len, size - len, 3);
	}

	template <typename T> void add(T value)
	{
		int len = Lumix::stringLength(data);
		Lumix::toCString(value, data + len, size - len);
	}

	bool operator<(const char* str) const {
		return Lumix::compareString(data, str) < 0;
	}

	bool operator==(const char* str) const {
		return Lumix::equalStrings(data, str);
	}

	operator const char*() const { return data; }
	char data[size];
};


template <class T> class base_string
{
public:
	explicit base_string(IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_cstr = nullptr;
		m_size = 0;
	}

	base_string(const base_string<T>& rhs, int start, i32 length)
		: m_allocator(rhs.m_allocator)
	{
		m_size = length - start <= rhs.m_size ? length : rhs.m_size - start;
		m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
		copyMemory(m_cstr, rhs.m_cstr + start, m_size * sizeof(T));
		m_cstr[m_size] = 0;
	}

	base_string(const T* rhs, i32 length, IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_size = length;
		m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
		copyMemory(m_cstr, rhs, m_size * sizeof(T));
		m_cstr[m_size] = 0;
	}

	base_string(const base_string<T>& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_cstr = (T*)m_allocator.allocate((rhs.m_size + 1) * sizeof(T));
		m_size = rhs.m_size;
		copyMemory(m_cstr, rhs.m_cstr, m_size * sizeof(T));
		m_cstr[m_size] = 0;
	}

	base_string(const T* rhs, IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_size = stringLength(rhs);
		m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
		copyMemory(m_cstr, rhs, sizeof(T) * (m_size + 1));
	}

	~base_string() { m_allocator.deallocate(m_cstr); }

	T operator[](int index)
	{
		ASSERT(index >= 0 && index < m_size);
		return m_cstr[index];
	}


	const T operator[](int index) const
	{
		ASSERT(index >= 0 && index < m_size);
		return m_cstr[index];
	}


	void set(const char* rhs, int size)
	{
		if (rhs < m_cstr || rhs >= m_cstr + m_size)
		{
			m_allocator.deallocate(m_cstr);
			m_size = size;
			m_cstr = (T*)m_allocator.allocate(m_size + 1);
			copyMemory(m_cstr, rhs, size);
			m_cstr[size] = '\0';
		}
	}

	void operator=(const base_string<T>& rhs)
	{
		if (&rhs != this)
		{
			m_allocator.deallocate(m_cstr);
			m_cstr = (T*)m_allocator.allocate((rhs.m_size + 1) * sizeof(T));
			m_size = rhs.m_size;
			copyMemory(m_cstr, rhs.m_cstr, sizeof(T) * (m_size + 1));
		}
	}

	void operator=(const T* rhs)
	{
		if (rhs < m_cstr || rhs >= m_cstr + m_size)
		{
			m_allocator.deallocate(m_cstr);
			if (rhs)
			{
				m_size = stringLength(rhs);
				m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
				copyMemory(m_cstr, rhs, sizeof(T) * (m_size + 1));
			}
			else
			{
				m_size = 0;
				m_cstr = nullptr;
			}
		}
	}

	bool operator!=(const base_string<T>& rhs) const
	{
		return this->compareString(rhs.m_cstr) != 0;
	}

	bool operator!=(const T* rhs) const { return this->compareString(rhs) != 0; }

	bool operator==(const base_string<T>& rhs) const
	{
		return this->compareString(rhs.m_cstr) == 0;
	}

	bool operator==(const T* rhs) const { return this->compareString(rhs) == 0; }

	bool operator<(const base_string<T>& rhs) const
	{
		return this->compareString(rhs.m_cstr) < 0;
	}

	bool operator>(const base_string<T>& rhs) const
	{
		return this->compareString(rhs.m_cstr) > 0;
	}

	int rfind(T c) const
	{
		i32 i = m_size - 1;
		while (i >= 0 && m_cstr[i] != c)
		{
			--i;
		}
		return i >= 0 ? i : npos;
	}

	int length() const { return m_size; }

	const T* c_str() const { return m_cstr; }

	base_string<T> substr(int start, int length) const
	{
		return base_string<T>(*this, start, length);
	}

	base_string<T>& cat(const char* value, int length)
	{
		if (value < m_cstr || value >= m_cstr + m_size)
		{
			if (m_cstr)
			{
				i32 new_size = m_size + length;
				T* new_cstr = (T*)m_allocator.allocate(new_size + 1);
				copyMemory(new_cstr, m_cstr, sizeof(T) * m_size + 1);
				m_allocator.deallocate(m_cstr);
				m_cstr = new_cstr;
				m_size = new_size;
				catNString(m_cstr, m_size + 1, value, length);
			}
			else
			{
				m_size = length;
				m_cstr = (T*)m_allocator.allocate(m_size + 1);
				copyNString(m_cstr, m_size + 1, value, length);
			}
		}
		return *this;
	}

	template <class V> base_string& cat(V value)
	{
		char tmp[30];
		toCString(value, tmp, 30);
		*this += tmp;
		return *this;
	}

	base_string& cat(float value)
	{
		char tmp[40];
		toCString(value, tmp, 30, 10);
		*this += tmp;
		return *this;
	}

	base_string& cat(char* value)
	{
		*this += value;
		return *this;
	}

	base_string& cat(const char* value)
	{
		*this += value;
		return *this;
	}

	template <class V1, class V2> base_string<T>& cat(V1 v1, V2 v2)
	{
		cat(v1);
		return cat(v2);
	}

	template <class V1, class V2, class V3>
	base_string<T>& cat(V1 v1, V2 v2, V3 v3)
	{
		cat(v1);
		return cat(v2, v3);
	}


	template <class V1, class V2, class V3, class V4>
	base_string<T>& cat(V1 v1, V2 v2, V3 v3, V4 v4)
	{
		cat(v1);
		return cat(v2, v3, v4);
	}

	template <class V1, class V2, class V3, class V4, class V5>
	base_string<T>& cat(V1 v1, V2 v2, V3 v3, V4 v4, V5 v5)
	{
		cat(v1);
		return cat(v2, v3, v4, v5);
	}

	template <class V1, class V2, class V3, class V4, class V5, class V6>
	base_string<T>& cat(V1 v1, V2 v2, V3 v3, V4 v4, V5 v5, V6 v6)
	{
		cat(v1);
		return cat(v2, v3, v4, v5, v6);
	}

	void operator+=(const T* rhs)
	{
		if (rhs < m_cstr || rhs >= m_cstr + m_size)
		{
			if (m_cstr)
			{
				i32 new_size = m_size + base_string<T>::stringLength(rhs);
				T* new_cstr = (T*)m_allocator.allocate(new_size + 1);
				copyMemory(new_cstr, m_cstr, sizeof(T) * m_size + 1);
				m_allocator.deallocate(m_cstr);
				m_cstr = new_cstr;
				m_size = new_size;
				catString(m_cstr, m_size + 1, rhs);
			}
			else
			{
				m_size = base_string<T>::stringLength(rhs);
				m_cstr = (T*)m_allocator.allocate(m_size + 1);
				copyString(m_cstr, m_size + 1, rhs);
			}
		}
	}

	void operator+=(const base_string<T>& rhs)
	{
		if (!rhs.m_cstr || this == &rhs)
		{
			return;
		}
		if (m_cstr)
		{
			i32 new_size = m_size + rhs.length();
			T* new_cstr = (T*)m_allocator.allocate(new_size + 1);
			copyMemory(new_cstr, m_cstr, sizeof(T) * m_size + 1);
			m_allocator.deallocate(m_cstr);
			m_cstr = new_cstr;
			m_size = new_size;

			catString(m_cstr, m_size + 1, rhs.m_cstr);
		}
		else
		{
			*this = rhs;
		}
	}

	void erase(i32 pos)
	{
		if (pos >= 0 && pos < m_size)
		{
			copyString(m_cstr + pos, m_size - pos, m_cstr + pos + 1);
			--m_size;
		}
	}

public:
	static const int npos = 0xffFFffFF;

private:
	static i32 stringLength(const T* rhs)
	{
		const T* c = rhs;
		while (*c)
		{
			++c;
		}
		return (i32)(c - rhs);
	}

	int compareString(const T* rhs) const
	{
		if (!m_cstr)
		{
			return rhs > 0;
		}
		const T* left = m_cstr;
		const T* right = rhs;

		while (*left == *right && *left != 0)
		{
			++left;
			++right;
		}
		return *left < *right ? -1 : (*left == *right ? 0 : 1);
	}


private:
	i32 m_size;
	T* m_cstr;
	IAllocator& m_allocator;
};


typedef base_string<char> string;


} // !namespace Lumix
