#pragma once

namespace Lumix
{

template <class T, int Size> class FixedArray
{
public:
	static_assert(Size > 0, "Size must be positive");

	const T& operator[](int index) const { return m_data[index]; }

	T& operator[](int index) { return m_data[index]; }

	const T* begin() const { return m_data; }

	const T* end() const { return m_data + Size; }

	T* data() { return m_data; }

	int size() const { return Size; }

private:
	T m_data[Size];
};

} // ~namespace Lumix
