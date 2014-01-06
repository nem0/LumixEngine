#pragma once

#include <array>

namespace Lux
{
	template <class T, size_t Size> 
	class StaticArray
	{
	public:

		typedef T								value_type;
		typedef StaticArray<value_type, Size>	my_type;
		typedef size_t							size_type;

	public:
		enum { elementSize = sizeof(value_type) };

		void assign(const value_type& val)
		{
			for(size_type i = 0; i < Size; ++i)
			{
				_a[i] = val;
			}
		}

		LUX_FORCE_INLINE size_type size() const 
		{
			return Size;
		}

		LUX_FORCE_INLINE size_type max_size() const 
		{
			return Size;
		}

		bool empty() const 
		{
			return 0 == Size;
		}

		value_type& at(size_type i) 
		{
			ASSERT(i >= 0 && i < Size);
			return _a[i];
		}

		const value_type& at(size_type i) const 
		{
			ASSERT(i >= 0 && i < Size);
			return _a[i];
		}

		value_type& operator[](size_type i) 
		{
			ASSERT(i >= 0 && i < Size);
			return _a[i];
		}

		const value_type& operator[](size_type i) const 
		{
			ASSERT(i >= 0 && i < Size);
			return _a[i];
		}

		value_type& front() 
		{
			return _a[0];
		}

		const value_type& front() const 
		{
			return _a[0];
		}

		value_type& back() 
		{
			return _a[Size - 1];
		}

		const value_type& back() const 
		{
			return _a[Size - 1];
		}

		size_type find(size_type from, size_type to, const value_type& val) const
		{
			ASSERT(size() >= to);
			for (size_type i = from; i < to; ++i)
			{
				if (_a[i] == val)
					return i;
			}

			return(-1);
		}

		size_type find(const value_type& val) const
		{
			return find(0, size(), val);
		}

		void swap(size_type idx1, size_type idx2)
		{
			ASSERT(idx1 < Size && idx2 < Size);

			if (idx1 != idx2)
			{
				value_type tmp = _a[idx1];
				_a[idx1] = _a[idx2];
				_a[idx2] = tmp;
			}
		}

		value_type* data() 
		{ 
			return _a;
		}

		const value_type* data() const
		{
			return _a;
		}

	private:
		void _fill(const value_type& val)
		{
			for(value_type* ptr = &front(); ptr < &back(); ++ptr)
				*ptr = val;
		}

		value_type _a[Size];
	};
	// not supported
	template <class T> class StaticArray<T, 0>;
} // ~namespace Lux
