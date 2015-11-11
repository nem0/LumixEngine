#pragma once

#include "core/MT/atomic.h"

namespace Lumix 
{
	namespace MT
	{
		template <class T, int32 size> class LockFreeQueue
		{
		public:

			LockFreeQueue();
			~LockFreeQueue();

			int32		push(T* data);
			T*			pop();
			bool		isEmpty() const;

		protected:
			struct Node
			{
				union
				{
					struct
					{
						int32 key;
						T*	el;
					};
					int64		val;
				};

				Node()
				{}

				Node(int32 k, T* el)
					: key(k)
					, el(el)
				{}
			};

			volatile int32	m_rd;
			volatile int32	m_wr;
			volatile int32	m_rd_alloc;
			volatile int32	m_wr_alloc;
			T*					m_pool[size];
			Node				m_queue[size];
		};

		template <class T, int32 size>		LockFreeQueue<T, size>::LockFreeQueue()
			: m_rd(0)
			, m_wr(0)
		{
			ASSERT(size > 0);
			ASSERT((size & (size - 1)) == 0); // power of two

			for (int32 i = 0; i < size; ++i)
			{
				m_queue[i].key = i;
				m_queue[i].el = nullptr;
			}
		}

		template <class T, int32 size>		LockFreeQueue<T, size>::~LockFreeQueue()
		{
		}

		template <class T, int32 size> bool LockFreeQueue<T, size>::isEmpty() const
		{
			return m_wr == m_rd;
		}

		template <class T, int32 size> int32 LockFreeQueue<T, size>::push(T* data)
		{
			ASSERT(data);

			Node cur_node(0, (T*)nullptr);
			Node new_node(0, data);

			while ((m_wr - m_rd) < size)
			{
				int32 cur_write_idx = m_wr;
				int32 idx = cur_write_idx & (size - 1);

				cur_node.key = cur_write_idx;
				new_node.key = cur_write_idx;

				if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
				{
					atomicIncrement(&m_wr);
					return idx;
				}
			}

			return -1;
		}

		template <class T, int32 size> T* LockFreeQueue<T, size>::pop()
		{
			while (m_rd != m_wr)
			{
				int cur_read_idx = m_rd;
				int32 idx = cur_read_idx & (size - 1);

				Node cur_node(cur_read_idx, m_queue[idx].el);
				Node new_node(cur_read_idx + size, nullptr);

				if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
				{
					atomicIncrement(&m_rd);
					return cur_node.el;
				}
			}

			return (T*)nullptr;
		}

	} // ~namespace MT
} // ~namespace Lumix

