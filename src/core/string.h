#pragma once


#include "core/lux.h"
#include <cstring>


namespace Lux
{
	

template <class T>
class base_string
{
	public:
		static base_string<T> create(unsigned int length, const char *s)
		{
			return base_string<T>(s);
		}

		base_string()
		{
			m_cstr = 0;
			m_size = 0;
		}

		base_string(const base_string<T>& rhs, int start, size_t length)
		{
			m_size = length - start <= rhs.m_size ? length : rhs.m_size - start;
			m_cstr = new T[m_size + 1];
			memcpy(m_cstr, rhs.m_cstr + start, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}
		
		base_string(const base_string<T>& rhs)
		{
			m_cstr = new T[rhs.m_size+1];
			m_size = rhs.m_size;
			memcpy(m_cstr, rhs.m_cstr, m_size * sizeof(T));
			m_cstr[m_size] = 0;
		}

		base_string(const T* rhs)
		{
			m_size = strlen(rhs);
			m_cstr = new T[m_size + 1];
			memcpy(m_cstr, rhs, sizeof(T) * (m_size + 1));
		}

		~base_string()
		{
			delete[] m_cstr;
		}

		void operator = (const base_string<T>& rhs) 
		{
			if(&rhs != this)
			{
				delete[] m_cstr;
				m_cstr = new T[rhs.m_size + 1];
				m_size = rhs.m_size;
				memcpy(m_cstr, rhs.m_cstr, sizeof(T) * (m_size + 1));
			}
		}

		void operator = (const T* rhs) 
		{
			delete[] m_cstr;
			m_size = strlen(rhs);
			m_cstr = new T[m_size + 1];
			memcpy(m_cstr, rhs, sizeof(T) * (m_size + 1));
		}

		bool operator !=(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.m_cstr) != 0;
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
			int i = m_size - 1;
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
		
		void operator += (const T* rhs)
		{
			if(m_cstr)
			{
				m_size += base_string<T>::strlen(rhs);
				T* newStr = new T[m_size+1];
				base_string<T>::strcpy(newStr, m_cstr);
				base_string<T>::strcat(newStr, rhs);
				delete[] m_cstr;
				m_cstr = newStr;
			}
			else
			{
				m_size = base_string<T>::strlen(rhs);
				T* newStr = new T[m_size+1];
				base_string<T>::strcpy(newStr, rhs);
				m_cstr = newStr;
			}
		}

		void operator += (const base_string<T>& rhs)
		{
			m_size += rhs.m_size;
			T* newStr = new T[m_size];
			base_string<T>::strcpy(newStr, m_cstr);
			base_string<T>::strcat(newStr, rhs.m_cstr);
			delete[] m_cstr;
			m_cstr = newStr;
		}

		base_string<T> operator +(const base_string<T>& rhs)
		{
			base_string<T> ret = *this;
			ret += rhs;
			return ret;
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

		static int strlen(const T* rhs) 
		{
			const T* c = rhs;
			while(*c)
			{
				++c;
			}
			return c - rhs;
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
		size_t m_size;
		T*	m_cstr;
};


typedef base_string<char> string;


} // !namespace Lux