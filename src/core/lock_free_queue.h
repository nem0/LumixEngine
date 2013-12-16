#pragma once

#include "platform/atomic.h"

namespace Lux 
{
	namespace MT
	{
		template <class T, int32_t size> class LockFreeQueue
		{
		public:

			LockFreeQueue();
			~LockFreeQueue();

			int32_t		push(T* data);
			T*			pop();
			bool		isEmpty() const;

		protected:
			struct Node
			{
				union
				{
					struct
					{
						int32_t key;
						T*		el;
					};
					int64_t		val;
				};

				Node()
				{}

				Node(int32_t k, T* v)
					: key(k)
					, el(v)
				{}
			};

			volatile int32_t	m_rd;
			volatile int32_t	m_wr;
			Node				m_queue[size];
		};

		template <class T, int32_t size>		LockFreeQueue<T, size>::LockFreeQueue()
			: m_rd(0)
			, m_wr(0)
		{
			ASSERT(size > 0);
			ASSERT((size & (size - 1)) == 0); // power of two

			for (int32_t i = 0; i < size; ++i)
			{
				m_queue[i].key = i;
				m_queue[i].el = NULL;
			}
		}

		template <class T, int32_t size>		LockFreeQueue<T, size>::~LockFreeQueue()
		{
		}

		template <class T, int32_t size> bool LockFreeQueue<T, size>::isEmpty() const
		{
			return m_wr == m_rd;
		}

		template <class T, int32_t size> int32_t LockFreeQueue<T, size>::push(T* data)
		{
			ASSERT(NULL != data);

			Node cur_node(0, (T*)NULL);
			Node new_node(0, data);

			while ((m_wr - m_rd) < size)
			{
				int32_t cur_write_idx = m_wr;
				int32_t idx = cur_write_idx & (size - 1);

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

		template <class T, int32_t size> T* LockFreeQueue<T, size>::pop()
		{
			while (m_rd != m_wr)
			{
				int cur_read_idx = m_rd;
				int32_t idx = cur_read_idx & (size - 1);

				Node cur_node(cur_read_idx, m_queue[idx].el);
				Node new_node(cur_read_idx + size, NULL);

				if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
				{
					atomicIncrement(&m_rd);
					return cur_node.el;
				}
			}

			return (T*)NULL;
		}

	} // ~namespace MT
} // ~namespace Lux

