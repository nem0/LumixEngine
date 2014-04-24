#pragma once

#include "core/atomic.h"
#include "core/event.h"
#include "core/semaphore.h"

namespace Lux
{
	namespace MT
	{
		template <class T> struct Transaction 
		{
			static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
			void setCompleted()		{ m_event.trigger();		}
			bool isCompleted()		{ return m_event.poll();	}
			void waitForCompletion() { return m_event.wait();	}
			void reset()	{ m_event.reset(); }

			Transaction() : m_event(MT::EventFlags::MANUAL_RESET) { }

			MT::Event	m_event;
			T			data;
		};

		template <class T, int32_t size> class TransactionQueue 
		{
		public:
			TransactionQueue()
				: m_al(0)
				, m_fr(0)
				, m_rd(0)
				, m_wr(0)
				, m_aborted(false)
				, m_data_signal(0, size)
			{
				for (int32_t i = 0; i < size; i++)
				{
					m_alloc[i].key = i;
					m_alloc[i].el = i;
					m_queue[i].key = i;
					m_queue[i].el = -1;
				}
			}

			~TransactionQueue()
			{
			}

			T* alloc(bool wait)
			{
				do
				{
					int32_t alloc_ptr = m_al;
					int32_t alloc_idx = alloc_ptr & (size - 1);

					Node cur_val(alloc_ptr, m_alloc[alloc_idx].el);

					if (cur_val.el > -1)
					{
						Node new_val(alloc_ptr, -1);

						if (compareAndExchange64(&m_alloc[alloc_idx].val, new_val.val, cur_val.val))
						{
							atomicIncrement(&m_al);
							return &m_pool[cur_val.el];
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

					cur_val.key = free_ptr;
					new_val.key = free_ptr + size;
					if (compareAndExchange64(&m_alloc[free_idx].val, new_val.val, cur_val.val))
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
					Node cur_node(0, -1);
					Node new_node(0, idx);

					int32_t cur_write_idx = m_wr;
					int32_t idx = cur_write_idx & (size - 1);

					cur_node.key = cur_write_idx;
					new_node.key = cur_write_idx;

					if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
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
				do
				{
					if (wait)
					{
						m_data_signal.wait();
						if (isAborted())
						{
							return NULL;
						}
					}

					while (m_rd != m_wr)
					{
						int cur_read_idx = m_rd;
						int32_t idx = cur_read_idx & (size - 1);

						Node cur_node(cur_read_idx, m_queue[idx].el);

						if (cur_node.el > -1)
						{
							Node new_node(cur_read_idx + size, -1);

							if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
							{
								atomicIncrement(&m_rd);
								return &m_pool[cur_node.el];
							}
						}
					}
				} while (wait);

				return NULL;
			}


			bool isAborted() const
			{
				return m_aborted;
			}

			bool isEmpty() const
			{
				return m_al == m_fr;
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
					};
					int64_t		val;
				};

				Node()
				{
				}

				Node(int32_t k, int32_t i)
					: key(k)
					, el(i)
				{
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
	} // ~namespace MT
} // ~namespace Lux
