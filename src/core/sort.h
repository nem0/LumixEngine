#pragma once

namespace Lumix {
	template <typename T>
	void insertSort(T* from, T* to) {
		if (from >= to) return;

		for (T* i = from + 1; i < to; ++i) {
			T key = static_cast<T&&>(*i);
			T* j = i - 1;
			
			while (j >= from && key < *j) {
				*(j + 1) = static_cast<T&&>(*j);
				--j;
			}
			
			*(j + 1) = static_cast<T&&>(key);
		}
	}

	template <typename T, typename LessThan>
	void insertSort(T* from, T* to, LessThan lessThan) {
		if (from >= to) return;

		for (T* i = from + 1; i < to; ++i) {
			T key = static_cast<T&&>(*i);
			T* j = i - 1;
			
			while (j >= from && lessThan(key, *j)) {
				*(j + 1) = static_cast<T&&>(*j);
				--j;
			}
			
			*(j + 1) = static_cast<T&&>(key);
		}
	}

	#define LUMIX_SWAP(a, b) \
	do { \
		T tmp = static_cast<T&&>(a); \
		(a) = static_cast<T&&>(b); \
		(b) = static_cast<T&&>(tmp); \
	} while(false)

	template <typename T, typename LessThan>
	int partition(T* from,  T* to, LessThan lessThan) {
		T* pivot = to - 1;
		T* i = from - 1;

		for (T* j = from; j < pivot; ++j) {
			if (lessThan(*j, *pivot)) {
				++i;
				LUMIX_SWAP(*i, *j);
			}
		}

		LUMIX_SWAP(*(i + 1), *pivot);
		return static_cast<int>(i + 1 - from);
	}

	template <typename T>
	int partition(T* from,  T* to) {
		T* mid = from + (to - from) / 2;
		if (*mid < *from) LUMIX_SWAP(*mid, *from);
		if (*(to - 1) < *from) LUMIX_SWAP(*(to - 1), *from);
		if (*(to - 1) < *mid) LUMIX_SWAP(*(to - 1), *mid);
		T* pivot = mid;
		T* i = from - 1;

		for (T* j = from; j < pivot; ++j) {
			if (*j < *pivot) {
				++i;
				LUMIX_SWAP(*i, *j);
			}
		}

		LUMIX_SWAP(*(i + 1), *pivot);
		return static_cast<int>(i + 1 - from);
	}

	template <typename T, typename LessThan>
	void sort(T* from, T* to, LessThan lessThan, u32 depth = 0) {
		if (from >= to) return;
		if (to - from <= 32 || depth > 25) {
			insertSort(from, to, lessThan);
			return;
		}
		
		int pivot_pos = partition(from, to, lessThan);
		sort(from, from + pivot_pos, lessThan, depth + 1);
		sort(from + pivot_pos + 1, to, lessThan, depth + 1);
	}

	#undef LUMIX_SWAP

	template <typename T>
	void sort(T* from, T* to, u32 depth = 0) {
		if (from >= to) return;
		if (to - from <= 32 || depth > 25) {
			insertSort(from, to);
			return;
		}
		
		int pivot_pos = partition(from, to);
		sort(from, from + pivot_pos, depth + 1);
		sort(from + pivot_pos + 1, to, depth + 1);
	}
}