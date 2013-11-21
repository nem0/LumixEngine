#pragma once


#include "core/lux.h"
#include <cstring>
#include <cassert>


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
			mCStr = 0;
			mSize = 0;
		}

		base_string(const base_string<T>& rhs, int start, size_t length)
		{
			mSize = length - start <= rhs.mSize ? length : rhs.mSize - start;
			mCStr = new T[mSize + 1];
			memcpy(mCStr, rhs.mCStr + start, mSize * sizeof(T));
			mCStr[mSize] = 0;
		}
		
		base_string(const base_string<T>& rhs)
		{
			mCStr = new T[rhs.mSize+1];
			mSize = rhs.mSize;
			memcpy(mCStr, rhs.mCStr, mSize * sizeof(T));
			mCStr[mSize] = 0;
		}

		base_string(const T* rhs)
		{
			mSize = strlen(rhs);
			mCStr = new T[mSize + 1];
			memcpy(mCStr, rhs, sizeof(T) * (mSize + 1));
		}

		~base_string()
		{
			delete[] mCStr;
		}

		void operator = (const base_string<T>& rhs) 
		{
			if(&rhs != this)
			{
				delete[] mCStr;
				mCStr = new T[rhs.mSize + 1];
				mSize = rhs.mSize;
				memcpy(mCStr, rhs.mCStr, sizeof(T) * (mSize + 1));
			}
		}

		void operator = (const T* rhs) 
		{
			delete[] mCStr;
			mSize = strlen(rhs);
			mCStr = new T[mSize + 1];
			memcpy(mCStr, rhs, sizeof(T) * (mSize + 1));
		}

		bool operator !=(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.mCStr) != 0;
		}

		bool operator ==(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.mCStr) == 0;
		}

		bool operator ==(const T* rhs) const
		{
			return this->strcmp(rhs) == 0;
		}

		bool operator <(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.mCStr) < 0;
		}

		bool operator >(const base_string<T>& rhs) const
		{
			return this->strcmp(rhs.mCStr) > 0;
		}
		
		int rfind(T c) const
		{
			int i = mSize - 1;
			while(i >= 0 && mCStr[i] != c)
			{
				--i;
			}
			return i >= 0 ? i : npos;
		}

		int length() const { return mSize; }

		const T* c_str() const { return mCStr; }
		
		base_string<T> substr(int start, int length) const
		{
			return base_string<T>(*this, start, length);
		}
		
		void operator += (const T* rhs)
		{
			if(mCStr)
			{
				mSize += base_string<T>::strlen(rhs);
				T* newStr = new T[mSize+1];
				base_string<T>::strcpy(newStr, mCStr);
				base_string<T>::strcat(newStr, rhs);
				delete[] mCStr;
				mCStr = newStr;
			}
			else
			{
				mSize = base_string<T>::strlen(rhs);
				T* newStr = new T[mSize+1];
				base_string<T>::strcpy(newStr, rhs);
				mCStr = newStr;
			}
		}

		void operator += (const base_string<T>& rhs)
		{
			mSize += rhs.mSize;
			T* newStr = new T[mSize];
			base_string<T>::strcpy(newStr, mCStr);
			base_string<T>::strcat(newStr, rhs.mCStr);
			delete[] mCStr;
			mCStr = newStr;
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
			if(!mCStr)
			{
				return rhs > 0;
			}
			const T* left = mCStr;
			const T* right = rhs;

			while(*left == *right && *left != 0)
			{
				++left;
				++right;
			}
			return *left < *right ? -1 : (*left == *right ? 0 : 1);
		}


	private:
		size_t mSize;
		T*	mCStr;
};


typedef base_string<char> string;


} // !namespace Lux