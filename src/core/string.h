#pragma once


#include "core/default_allocator.h"
#include "core/lumix.h"


namespace Lumix
{


LUMIX_CORE_API bool toCStringHex(uint8_t value, char* output, int length);
LUMIX_CORE_API bool toCString(int32_t value, char* output, int length);
LUMIX_CORE_API bool toCString(int64_t value, char* output, int length);
LUMIX_CORE_API bool toCString(uint32_t value, char* output, int length);
LUMIX_CORE_API bool toCString(float value, char* output, int length, int after_point);

LUMIX_CORE_API bool fromCString(const char* input, int length, int32_t* value);
LUMIX_CORE_API bool fromCString(const char* input, int length, int64_t* value);
LUMIX_CORE_API bool fromCString(const char* input, int length, uint32_t* value);
LUMIX_CORE_API bool copyString(char* destination, int length, const char* source);
LUMIX_CORE_API bool catCString(char* destination, int length, const char* source);

template <class T>
class base_string
{
	public:
		base_string(IAllocator& allocator)
			: m_allocator(allocator)
		{
			m_cstr = NULL;
			m_size = 0;
		}

		base_string(const base_string<T>& rhs, int start, int32_t length)
			: m_allocator(rhs.m_allocator)
		{
			m_size = length - start <= rhs.m_size ? length : rhs.m_size - start;
			m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
			memcpy(m_cstr, rhs.m_cstr + start, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}
		
		base_string(IAllocator& allocator, const char* rhs, int32_t length)
			: m_allocator(allocator)
		{
			m_size = length;
			m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
			memcpy(m_cstr, rhs, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}

		base_string(const base_string<T>& rhs)
			: m_allocator(rhs.m_allocator)
		{
			m_cstr = (T*)m_allocator.allocate((rhs.m_size + 1) * sizeof(T));
			m_size = rhs.m_size;
			memcpy(m_cstr, rhs.m_cstr, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}

		explicit base_string(const T* rhs, IAllocator& allocator)
			: m_allocator(allocator)
		{
			m_size = strlen(rhs);
			m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
			memcpy(m_cstr, rhs, sizeof(T) * (m_size + 1));
		}

		~base_string()
		{
			m_allocator.deallocate(m_cstr);
		}

		T operator[](int index)
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
				memcpy(m_cstr, rhs, size);
				m_cstr[size] = '\0';
			}
		}

		void operator = (const base_string<T>& rhs) 
		{
			if(&rhs != this)
			{
				m_allocator.deallocate(m_cstr);
				m_cstr = (T*)m_allocator.allocate((rhs.m_size + 1) * sizeof(T));
				m_size = rhs.m_size;
				memcpy(m_cstr, rhs.m_cstr, sizeof(T) * (m_size + 1));
			}
		}

		void operator = (const T* rhs) 
		{
			if(rhs < m_cstr || rhs >= m_cstr + m_size)
			{
				m_allocator.deallocate(m_cstr);
				if(rhs)
				{
					m_size = strlen(rhs);
					m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
					memcpy(m_cstr, rhs, sizeof(T) * (m_size + 1));
				}
				else
				{
					m_size = 0;
					m_cstr = NULL;
				}
			}
		}

		bool operator !=(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) != 0;
		}

		bool operator !=(const T* rhs) const
		{
			return this->strcmp(rhs) != 0;
		}

		bool operator ==(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) == 0;
		}

		bool operator ==(const T* rhs) const
		{
			return this->strcmp(rhs) == 0;
		}

		bool operator <(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) < 0;
		}

		bool operator >(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) > 0;
		}
		
		int rfind(T c) const
		{
			int32_t i = m_size - 1;
			while(i >= 0 && m_cstr[i] != c)
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
		
		template <class V>
		base_string<T>& cat(V value)
		{
			char tmp[30];
			toCString(value, tmp, 30);
			*this += tmp;
			return *this;
		}

		template<>
		base_string<T>& cat<float>(float value)
		{
			char tmp[40];
			toCString(value, tmp, 30, 10);
			*this += tmp;
			return *this;
		}

		template<>
		base_string<T>& cat<char*>(char* value)
		{
			*this += value;
			return *this;
		}

		template<>
		base_string<T>& cat<const char*>(const char* value)
		{
			*this += value;
			return *this;
		}

		template <class V1, class V2>
		base_string<T>& cat(V1 v1, V2 v2)
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

		void operator += (const T* rhs)
		{
			if(rhs < m_cstr || rhs >= m_cstr + m_size)
			{
				if(m_cstr)
				{
					int32_t new_size = m_size + base_string<T>::strlen(rhs);
					T* new_cstr = (T*)m_allocator.allocate(new_size + 1);
					memcpy(new_cstr, m_cstr, sizeof(T) * m_size + 1);
					m_allocator.deallocate(m_cstr);
					m_cstr = new_cstr;
					m_size = new_size;
					catCString(m_cstr, m_size + 1, rhs);
				}
				else
				{
					m_size = base_string<T>::strlen(rhs);
					m_cstr = (T*)m_allocator.allocate(m_size + 1);
					copyString(m_cstr, m_size + 1, rhs);
				}
			}
		}

		void operator += (const base_string<T>& rhs)
		{
			if(!rhs.m_cstr || this == &rhs)
			{
				return;
			}
			if(m_cstr)
			{
				int32_t new_size = m_size + rhs.length();
				T* new_cstr = (T*)m_allocator.allocate(new_size + 1);
				memcpy(new_cstr, m_cstr, sizeof(T) * m_size + 1);
				m_allocator.deallocate(m_cstr);
				m_cstr = new_cstr;
				m_size = new_size;

				catCString(m_cstr, m_size + 1, rhs.m_cstr);
			}
			else
			{
				*this = rhs;
			}
		}

		void erase(int32_t pos)
		{
			if(pos >= 0 && pos < m_size)
			{
				copyString(m_cstr + pos, m_size - pos, m_cstr + pos + 1);
				--m_size;
			}
		}

	public:
		static const int npos = 0xffFFffFF;

	private:
		static int32_t strlen(const T* rhs) 
		{
			const T* c = rhs;
			while(*c)
			{
				++c;
			}
			return (int32_t)(c - rhs);
		}

		int strcmp(const T* rhs) const
		{
			if(!m_cstr)
			{
				return rhs > 0;
			}
			const T* left = m_cstr;
			const T* right = rhs;

			while(*left == *right && *left != 0)
			{
				++left;
				++right;
			}
			return *left < *right ? -1 : (*left == *right ? 0 : 1);
		}


	private:
		int32_t m_size;
		T*	m_cstr;
		IAllocator& m_allocator;
};


typedef base_string<char> string;


} // !namespace Lumix
