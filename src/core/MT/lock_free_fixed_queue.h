#pragma once

#include "core/iallocator.h"
#include "core/mt/atomic.h"
#include "core/mt/semaphore.h"

namespace Lumix
{
	namespace MT
	{
		template <class T, int32_t size, bool is_trivial = std::is_trivial<T>::value> class LockFreeFixedQueue;

		template <class T, int32_t size, bool is_trivial>
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
				for (int32_t i = 0; i < size; i++)
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
						int32_t alloc_ptr = m_al;
						int32_t alloc_idx = alloc_ptr & (size - 1);

						Node cur_val(alloc_ptr, m_alloc[alloc_idx].data.pair.el);

						if (cur_val.data.pair.el > -1)
						{
							Node new_val(alloc_ptr, -1);

							if (compareAndExchange64(&m_alloc[alloc_idx].data.val, new_val.data.val, cur_val.data.val))
							{
								atomicIncrement(&m_al);
								return &m_pool[cur_val.data.pair.el];
							}
						}
					}
				} while (wait);

				return NULL;
			}

			void dealoc(T* tr, bool wait)
			{
				int32_t idx = int32_t(tr - m_pool);
				ASSERT(idx >= 0 && idx < size);

				Node cur_val(0, -1);
				Node new_val(0, idx);

				do
				{
					int32_t free_ptr = m_fr;
					int32_t free_idx = free_ptr & (size - 1);

					cur_val.data.pair.key = free_ptr;
					new_val.data.pair.key = free_ptr + size;
					if (compareAndExchange64(&m_alloc[free_idx].data.val, new_val.data.val, cur_val.data.val))
					{
						atomicIncrement(&m_fr);
						break;
					}
				} while (wait);
			}

			bool push(const T* tr, bool wait)
			{
				int32_t idx = int32_t(tr - m_pool);
				ASSERT(idx >= 0 && idx < size);

				do
				{
					ASSERT((m_wr - m_rd) < size);

					Node cur_node(0, -1);
					Node new_node(0, idx);

					int32_t cur_write_idx = m_wr;
					int32_t idx = cur_write_idx & (size - 1);

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
					return NULL;
				}

				while (can_read)
				{
					if (m_rd != m_wr)
					{
						int32_t cur_read_idx = m_rd;
						int32_t idx = cur_read_idx & (size - 1);

						Node cur_node(cur_read_idx, m_queue[idx].data.pair.el);

						if (cur_node.data.pair.el > -1)
						{
							Node new_node(cur_read_idx + size, -1);

							if (compareAndExchange64(&m_queue[idx].data.val, new_node.data.val, cur_node.data.val))
							{
								atomicIncrement(&m_rd);
								return &m_pool[cur_node.data.pair.el];
							}
						}
					}
				}

				return NULL;
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
						int32_t key;
						int32_t	el;
					} pair;
					int64_t		val;
				} data;

				Node()
				{
				}

				Node(int32_t k, int32_t i)
				{
					data.pair.key = k;
					data.pair.el = i;
				}
			};

			volatile int32_t	m_al;
			volatile int32_t	m_fr;
			volatile int32_t	m_rd;
			volatile int32_t	m_wr;
			Node				m_alloc[size];
			Node				m_queue[size];
			T					m_pool[size];
			volatile bool		m_aborted;
			MT::Semaphore		m_data_signal;
		};

		template <class T, int32_t size>
		class LockFreeFixedQueue<T, size, false>
		{
		public:
			LockFreeFixedQueue(IAllocator& allocator)
				: m_al(0)
				, m_fr(0)
				, m_rd(0)
				, m_wr(0)
				, m_aborted(false)
				, m_data_signal(0, size)
				, m_allocator(allocator)
			{
				m_pool = (T*)m_allocator.allocate(sizeof(T) * size);

				for (int32_t i = 0; i < size; i++)
				{
					m_alloc[i].data.pair.key = i;
					m_alloc[i].data.pair.el = i;
					m_queue[i].data.pair.key = i;
					m_queue[i].data.pair.el = -1;
				}
			}

			~LockFreeFixedQueue()
			{
				m_allocator.deallocate((char*)m_pool);
			}

			T* alloc(bool wait)
			{
				do
				{
					if ((m_al - m_fr) < size)
					{
						int32_t alloc_ptr = m_al;
						int32_t alloc_idx = alloc_ptr & (size - 1);

						Node cur_val(alloc_ptr, m_alloc[alloc_idx].data.pair.el);

						if (cur_val.data.pair.el > -1)
						{
							Node new_val(alloc_ptr, -1);

							if (compareAndExchange64(&m_alloc[alloc_idx].data.val, new_val.data.val, cur_val.data.val))
							{
								atomicIncrement(&m_al);
								new (&m_pool[cur_val.data.pair.el]) T();
								return &m_pool[cur_val.data.pair.el];
							}
						}
					}
				} while (wait);

				return NULL;
			}

			void dealoc(T* tr)
			{
				tr->~T();
				int32_t idx = int32_t(tr - m_pool);
				ASSERT(idx >= 0 && idx < size);

				Node cur_val(0, -1);
				Node new_val(0, idx);

				for (;;)
				{
					int32_t free_ptr = m_fr;
					int32_t free_idx = free_ptr & (size - 1);

					cur_val.data.pair.key = free_ptr;
					new_val.data.pair.key = free_ptr + size;
					if (compareAndExchange64(&m_alloc[free_idx].data.val, new_val.data.val, cur_val.data.val))
					{
						atomicIncrement(&m_fr);
						break;
					}
				}
			}

			bool push(const T* tr, bool wait)
			{
				int32_t idx = int32_t(tr - m_pool);
				ASSERT(idx >= 0 && idx < size);

				do
				{
					ASSERT((m_wr - m_rd) < size);

					Node cur_node(0, -1);
					Node new_node(0, idx);

					int32_t cur_write_idx = m_wr;
					int32_t idx = cur_write_idx & (size - 1);

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
					return NULL;
				}

				while (can_read)
				{
					if (m_rd != m_wr)
					{
						int32_t cur_read_idx = m_rd;
						int32_t idx = cur_read_idx & (size - 1);

						Node cur_node(cur_read_idx, m_queue[idx].data.pair.el);

						if (cur_node.data.pair.el > -1)
						{
							Node new_node(cur_read_idx + size, -1);

							if (compareAndExchange64(&m_queue[idx].data.val, new_node.data.val, cur_node.data.val))
							{
								atomicIncrement(&m_rd);
								return &m_pool[cur_node.data.pair.el];
							}
						}
					}
				}

				return NULL;
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
						int32_t key;
						int32_t	el;
					} pair;
					int64_t		val;
				} data;

				Node()
				{
				}

				Node(int32_t k, int32_t i)
				{
					data.pair.key = k;
					data.pair.el = i;
				}
			};

			IAllocator&			m_allocator;
			volatile int32_t	m_al;
			volatile int32_t	m_fr;
			volatile int32_t	m_rd;
			volatile int32_t	m_wr;
			Node				m_alloc[size];
			Node				m_queue[size];
			T*					m_pool;
			volatile bool		m_aborted;
			MT::Semaphore		m_data_signal;
		};
	} // ~namespace MT
} // ~namespace Lumix
