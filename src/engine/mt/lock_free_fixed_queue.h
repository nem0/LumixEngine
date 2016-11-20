#pragma once

#include "engine/iallocator.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"


namespace Lumix
{
	namespace MT
	{
		template <class T, i32 size>
		class LockFreeFixedQueue
		{
		public:
			LockFreeFixedQueue()
				: m_al(0)
				, m_fr(0)
				, m_rd(0)
				, m_wr(0)
				, m_aborted(false)
				, m_data_signal(0, size)
			{
				for (i32 i = 0; i < size; i++)
				{
					m_alloc[i].data.pair.key = i;
					m_alloc[i].data.pair.el = i;
					m_queue[i].data.pair.key = i;
					m_queue[i].data.pair.el = -1;
				}
			}

			~LockFreeFixedQueue()
			{
			}

			T* alloc(bool wait)
			{
				do
				{
					if ((m_al - m_fr) < size)
					{
						i32 alloc_ptr = m_al;
						i32 alloc_idx = alloc_ptr & (size - 1);

						Node cur_val(alloc_ptr, m_alloc[alloc_idx].data.pair.el);

						if (cur_val.data.pair.el > -1)
						{
							Node new_val(alloc_ptr, -1);

							if (compareAndExchange64(&m_alloc[alloc_idx].data.val, new_val.data.val, cur_val.data.val))
							{
								atomicIncrement(&m_al);
								auto* val = (T*)&m_pool[cur_val.data.pair.el * sizeof(T)];
								new (NewPlaceholder(), val) T();
								return val;
							}
						}
					}
				} while (wait);

				return nullptr;
			}

			void dealoc(T* tr)
			{
				tr->~T();
				i32 idx = i32(tr - (T*)m_pool);
				ASSERT(idx >= 0 && idx < size);

				Node cur_val(0, -1);
				Node new_val(0, idx);

				for(;;)
				{
					i32 free_ptr = m_fr;
					i32 free_idx = free_ptr & (size - 1);

					cur_val.data.pair.key = free_ptr;
					new_val.data.pair.key = free_ptr + size;
					if (compareAndExchange64(&m_alloc[free_idx].data.val, new_val.data.val, cur_val.data.val))
					{
						atomicIncrement(&m_fr);
						break;
					}
				};
			}

			bool push(const T* tr, bool wait)
			{
				i32 idx = i32(tr - (T*)m_pool);
				ASSERT(idx >= 0 && idx < size);

				do
				{
					ASSERT((m_wr - m_rd) < size);

					Node cur_node(0, -1);
					Node new_node(0, idx);

					i32 cur_write_idx = m_wr;
					i32 idx = cur_write_idx & (size - 1);

					cur_node.data.pair.key = cur_write_idx;
					new_node.data.pair.key = cur_write_idx;

					if (compareAndExchange64(&m_queue[idx].data.val, new_node.data.val, cur_node.data.val))
					{
						atomicIncrement(&m_wr);
						m_data_signal.signal();
						return true;
					}
				} while (wait);

				return false;
			}

			T* pop(bool wait)
			{
				bool can_read = wait ? m_data_signal.wait(), wait : m_data_signal.poll();

				if (isAborted())
				{
					return nullptr;
				}

				while (can_read)
				{
					if (m_rd != m_wr)
					{
						i32 cur_read_idx = m_rd;
						i32 idx = cur_read_idx & (size - 1);

						Node cur_node(cur_read_idx, m_queue[idx].data.pair.el);

						if (cur_node.data.pair.el > -1)
						{
							Node new_node(cur_read_idx + size, -1);

							if (compareAndExchange64(&m_queue[idx].data.val, new_node.data.val, cur_node.data.val))
							{
								atomicIncrement(&m_rd);
								return (T*)&m_pool[cur_node.data.pair.el * sizeof(T)];
							}
						}
					}
				}

				return nullptr;
			}


			bool isAborted() const
			{
				return m_aborted;
			}

			bool isEmpty() const
			{
				return m_rd == m_wr;
			}

			void abort()
			{
				m_aborted = true;
				m_data_signal.signal();
			}

		private:

			struct Node
			{
				union
				{
					struct
					{
						i32 key;
						i32	el;
					} pair;
					i64		val;
				} data;

				Node()
				{
				}

				Node(i32 k, i32 i)
				{
					data.pair.key = k;
					data.pair.el = i;
				}
			};

			volatile i32	m_al;
			volatile i32	m_fr;
			volatile i32	m_rd;
			volatile i32	m_wr;
			Node				m_alloc[size];
			Node				m_queue[size];
			u8				m_pool[sizeof(T) * size];
			volatile bool		m_aborted;
			MT::Semaphore		m_data_signal;
		};
	} // ~namespace MT
} // ~namespace Lumix
