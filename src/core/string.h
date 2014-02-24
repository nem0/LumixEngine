#pragma once


#include "core/default_allocator.h"
#include "core/lux.h"
#include <cstring>


namespace Lux
{
	

template <class T, typename Allocator = DefaultAllocator>
class base_string
{
	public:
		static base_string<T, Allocator> create(unsigned int length, const char *s)
		{
			return base_string<T, Allocator>(s);
		}

		base_string(const Allocator& allocator)
			: m_allocator(allocator)
		{
			m_cstr = NULL;
			m_size = 0;
		}

		base_string()
		{
			m_cstr = NULL;
			m_size = 0;
		}

		base_string(const base_string<T, Allocator>& rhs, int start, int32_t length)
		{
			m_size = length - start <= rhs.m_size ? length : rhs.m_size - start;
			m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
			memcpy(m_cstr, rhs.m_cstr + start, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}
		
		base_string(const base_string<T, Allocator>& rhs)
		{
			m_cstr = (T*)m_allocator.allocate((rhs.m_size + 1) * sizeof(T));
			m_size = rhs.m_size;
			memcpy(m_cstr, rhs.m_cstr, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}

		explicit base_string(const T* rhs)
		{
			m_size = strlen(rhs);
			m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
			memcpy(m_cstr, rhs, sizeof(T) * (m_size + 1));
		}

		~base_string()
		{
			m_allocator.deallocate(m_cstr);
		}

		void operator = (const base_string<T, Allocator>& rhs) 
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
				m_size = strlen(rhs);
				m_cstr = (T*)m_allocator.allocate((m_size + 1) * sizeof(T));
				memcpy(m_cstr, rhs, sizeof(T) * (m_size + 1));
			}
		}

		bool operator !=(const base_string<T, Allocator>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) != 0;
		}

		bool operator !=(const T* rhs) const
		{
			return this->strcmp(rhs) != 0;
		}

		bool operator ==(const base_string<T, Allocator>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) == 0;
		}

		bool operator ==(const T* rhs) const
		{
			return this->strcmp(rhs) == 0;
		}

		bool operator <(const base_string<T, Allocator>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) < 0;
		}

		bool operator >(const base_string<T, Allocator>& rhs) const
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
		
		base_string<T, Allocator> substr(int start, int length) const
		{
			return base_string<T, Allocator>(*this, start, length);
		}
		
		void operator += (const T* rhs)
		{
			if(rhs < m_cstr || rhs >= m_cstr + m_size)
			{
				if(m_cstr)
				{
					m_size += base_string<T>::strlen(rhs);
					m_cstr = (T*)m_allocator.reallocate(m_cstr, m_size + 1);
					base_string<T>::strcat(m_cstr, rhs);			
				}
				else
				{
					m_size = base_string<T>::strlen(rhs);
					m_cstr = (T*)m_allocator.allocate(m_size + 1);
					base_string<T>::strcpy(m_cstr, rhs);
				}
			}
		}

		void operator += (const base_string<T, Allocator>& rhs)
		{
			if(!rhs.m_cstr || this == &rhs)
			{
				return;
			}
			if(m_cstr)
			{
				m_size += rhs.length();
				m_cstr = (T*)m_allocator.reallocate(m_cstr, m_size + 1);
				base_string<T>::strcat(m_cstr, rhs.m_cstr);
			}
			else
			{
				*this = rhs;
			}
		}

		void insert(int32_t pos, T value)
		{
			m_cstr = (T*)m_allocator.reallocate(m_cstr, m_size + 2);
			for(int32_t i = m_size + 1; i > pos; --i)
			{
				m_cstr[i] = m_cstr[i-1];
			}
			m_cstr[pos] = value;
			++m_size;
		}

		void erase(int32_t pos)
		{
			if(pos >= 0 && pos < m_size)
			{
				base_string<T>::strcpy(m_cstr + pos, m_cstr + pos + 1);
				--m_size;
			}
		}

	public:
		static const int npos = 0xffFFffFF;

	private:
		static void strcat(T* desc, const T* src)
		{
			T* d = desc;
			while(*d)
			{
				++d;
			}
			const T* s = src;
			while(*s)
			{
				*d = *s;
				++s; 
				++d;
			}
			*d = 0;
		}

		static void strcpy(T* desc, const T* src)
		{
			T* d = desc;
			const T* s = src;
			while(*s)
			{
				*d = *s;
				++s; 
				++d;
			}
			*d = 0;
		}

		static void strncpy(T* desc, const T* src, size_t max_size)
		{
			T* d = desc;
			const T* s = src;
			while(*s && (size_t)(s - src) < max_size)
			{
				*d = *s;
				++s; 
				++d;
			}
			*d = 0;
		}


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
		Allocator m_allocator;
};


typedef base_string<char> string;


} // !namespace Lux