#pragma once


#include "core/lux.h"
#include <cstdlib>
#include <new>
#include <cassert>


template <class T>
class vector
{
	public:
		vector()
		{
			mData = 0;
			mCapacity = 0;
			mSize = 0;
		}

		vector(const vector<T>& rhs)
		{
			mCapacity = 0;
			mSize = 0;
			mData = 0;
			reserve(rhs.mCapacity);
			memcpy(mData, rhs.mData, sizeof(T) * rhs.mSize);
			mSize = rhs.mSize;
		}

		~vector()
		{
			for(int i = 0; i < mSize; ++i)
			{
				mData[i].~T();
			}
			delete[] (char*)mData;
		}

		void eraseFast(int index)
		{
			if(index >= 0 && index < mSize)
			{
				mData[index].~T();
				if(index != mSize - 1)
				{
					memmove(mData + index, mData + mSize - 1, sizeof(T));
				}
				--mSize;
			}
		}

		void erase(int index)
		{
			if(index >= 0 && index < mSize)
			{
				mData[index].~T();
				memmove(mData + index, mData + index + 1, sizeof(T) * (mSize - index - 1));
				--mSize;
			}
		}

		void push_back(const T& value)
		{
			if(mSize == mCapacity)
			{
				grow();
			}
			new ((char*)(mData+mSize)) T(value);
			++mSize;
		}

		bool empty() const { return mSize == 0; }

		void clear()
		{
			for(int i = 0; i < mSize; ++i)
			{
				mData[i].~T();
			}
			mSize = 0;
		}

		T& push_back_empty()
		{
			if(mSize == mCapacity)
			{
				grow();
			}
			new ((char*)(mData+mSize)) T();
			++mSize;
			return mData[mSize-1];
		}


		const T& back() const
		{
			return mData[mSize-1];
		}


		T& back()
		{
			return mData[mSize-1];
		}


		void pop_back()
		{
			if(mSize > 0)
			{
				mData[mSize-1].~T();
				--mSize;
			}
		}

		void set_size(int size)
		{
			if(size <= mCapacity)
			{
				mSize = size;
			}
		}

		void resize(int size)
		{
			if(size > mCapacity)
			{
				reserve(size);
			}
			for(int i = mSize; i < size; ++i)
			{
				new ((char*)(mData+i)) T();
			}
			for(int i = size; i < mSize; ++i)
			{
				mData[i].~T();
			}
			mSize = size;
		}

		void reserve(int capacity)
		{
			if(capacity > mCapacity)
			{
				T* newData = (T*)new char[capacity * sizeof(T)];
				memcpy(newData, mData, mSize * sizeof(T));
				delete[] ((char*)mData);
				mData = newData;
				mCapacity = capacity;			
			}
		}

		const T& operator[] (int index) const { assert(index < mSize); return mData[index]; }
		T& operator[](int index) { return mData[index]; }
 		int size() const { return mSize; }
		int capacity() const { return mCapacity; }

		void operator =(const vector<T>& rhs) 
		{
			if(mCapacity < rhs.mSize)
			{
				delete[] mData;
				mData = (T*)new char[rhs.mSize * sizeof(T)];
				mCapacity = rhs.mSize;
			}
			mSize = rhs.mSize;
			if(mSize > 0)
			{
				memcpy(mData, rhs.mData, sizeof(T) * rhs.mSize);
			}
		}
	private:
		void* operator &() { return 0; }

		void grow()
		{
			int newCapacity = mCapacity == 0 ? 4 : mCapacity * 2;
			T* newData = (T*)new char[newCapacity * sizeof(T)];
			memcpy(newData, mData, mSize * sizeof(T));
			delete[] ((char*)mData);
			mData = newData;
			mCapacity = newCapacity;
		}

	private:
		int mCapacity;
		int mSize;
		T* mData;
};
