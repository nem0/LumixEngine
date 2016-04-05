#include "binary_array.h"
#include "core/iallocator.h"
#include "core/string.h"


namespace Lumix
{

static const int BITMASK_7BIT = sizeof(int32) * 8 - 1;

BinaryArray::StoreType BinaryArray::BINARY_MASK[32];
BinaryArray::StoreType BinaryArray::INDEX_BIT[32];


BinaryArray::Accessor::Accessor(BinaryArray& array, int index)
	: m_array(array)
	, m_index(index)
{
}


BinaryArray::BinaryArray(IAllocator& allocator)
	: m_allocator(allocator)
	, m_data(nullptr)
	, m_size(0)
	, m_capacity(0)
{
	static bool init = false;
	if (!init)
	{
		init = true;
		for (int i = BITMASK_7BIT; i >= 0; --i)
		{
			BINARY_MASK[i] = i == BITMASK_7BIT ? 0xffffFFFF : BINARY_MASK[i + 1]
																  << 1;
			INDEX_BIT[i] = 1 << (BITMASK_7BIT - i);
		}
	}
}


BinaryArray::~BinaryArray()
{
	m_allocator.deallocate(m_data);
}


void BinaryArray::eraseFast(int index)
{
	this->operator[](index) = back();
	pop();
}


void BinaryArray::erase(int index)
{
	if (0 <= index && index < m_size)
	{
		int major = index >> 5;
		const int last_major = (m_size - 1) >> 5;
		m_data[major] =
			((index & BITMASK_7BIT) == 0
				 ? 0
				 : (m_data[major] & BINARY_MASK[(index & BITMASK_7BIT) - 1])) |
			((m_data[major] & ~BINARY_MASK[index & BITMASK_7BIT]) << 1);
		if (major < last_major)
		{
			m_data[major] |= (m_data[major + 1] & 0x80000000) >> BITMASK_7BIT;
		}
		for (int i = major + 1; i <= last_major; ++i)
		{
			m_data[i] <<= 1;
			if (i < (m_size >> 5))
			{
				m_data[i] |= (m_data[i + 1] & 0x80000000) >> BITMASK_7BIT;
			}
		}
		--m_size;
	}
}


void BinaryArray::clear()
{
	m_size = 0;
}


void BinaryArray::push(bool value)
{
	if (m_capacity == m_size)
	{
		grow(m_capacity > 0 ? (m_capacity << 1) : 32);
	}
	if (value)
	{
		m_data[m_size >> 5] |= INDEX_BIT[m_size & BITMASK_7BIT];
	}
	else
	{
		m_data[m_size >> 5] &= ~INDEX_BIT[m_size & BITMASK_7BIT];
	}
	++m_size;
}


void BinaryArray::pop()
{
	ASSERT(m_size > 0);
	--m_size;
}


bool BinaryArray::operator[](int index) const
{
	ASSERT(index < m_size);
	return (m_data[index >> 5] & INDEX_BIT[index & BITMASK_7BIT]) > 0;
}


BinaryArray::Accessor BinaryArray::operator[](int index)
{
	return Accessor(*this, index);
}


bool BinaryArray::back() const
{
	ASSERT(m_size > 0);
	return (*this)[m_size - 1];
}


BinaryArray::Accessor BinaryArray::back()
{
	ASSERT(m_size > 0);
	return (*this)[m_size - 1];
}


int BinaryArray::size() const
{
	return m_size;
}


void BinaryArray::reserve(int capacity)
{
	if (((m_capacity + BITMASK_7BIT) >> 5) < ((capacity + BITMASK_7BIT) >> 5))
	{
		grow(capacity);
	}
}


void BinaryArray::resize(int size)
{
	reserve((size + BITMASK_7BIT) & ~BITMASK_7BIT);
	m_size = size;
}


BinaryArray::StoreType* BinaryArray::getRaw()
{
	return m_data;
}


int BinaryArray::getRawSize() const
{
	return (m_size + BITMASK_7BIT) >> 5;
}


void BinaryArray::grow(int capacity)
{
	StoreType* new_data =
		static_cast<StoreType*>(m_allocator.allocate(capacity >> 3));
	if (m_data)
	{
		copyMemory(new_data, m_data, sizeof(StoreType) * (m_size + BITMASK_7BIT) >> 5);
		m_allocator.deallocate(m_data);
	}
	m_data = new_data;
	m_capacity = capacity;
}
} // namespace Lumix