#pragma once

#include "core/semaphore.h"

namespace Lux
{
	namespace MT
	{
		template <class T, int32_t size> class BlockingQueue 
		{
		public:

			BlockingQueue();
			~BlockingQueue();

			int32_t		push(T* data, bool wait);
			int32_t		pop(T*& data, bool wait);
			bool		isEmpty() const;

			bool		isAborted() const;
			void		abort();
			void		abortFromService();

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

			Semaphore			m_wr_semaphore;
			Semaphore			m_rd_semaphore;
			volatile int32_t	m_rd;
			volatile int32_t	m_wr;
			volatile bool		m_aborted;
			Node				m_queue[size];
		};

		template <class T, int32_t size>		BlockingQueue<T,size>::BlockingQueue()
			: m_wr_semaphore(size, size)
			, m_rd_semaphore(0, size)
			, m_rd(0)
			, m_wr(0)
			, m_aborted(false)
		{
			ASSERT(size > 0);
			

			for (int32_t i = 0; i < size; ++i)
			{
				m_queue[i].key = i;
				m_queue[i].el = NULL;
			}
		}

		template <class T, int32_t size>		BlockingQueue<T,size>::~BlockingQueue()
		{
		}

		template <class T, int32_t size> void	BlockingQueue<T,size>::abort()
		{
			m_wr_semaphore.wait();
			m_rd_semaphore.signal();
		}

		template <class T, int32_t size> void	BlockingQueue<T,size>::abortFromService()
		{
			m_aborted = true;
		}

		template <class T, int32_t size> bool	BlockingQueue<T,size>::isAborted() const
		{
			return m_aborted;
		}

		template <class T, int32_t size> bool BlockingQueue<T,size>::isEmpty() const
		{
			return m_wr == m_rd;
		}

		template <class T, int32_t size> int32_t BlockingQueue<T,size>::push(T* data, bool wait)
		{
			ASSERT(NULL != data);

			if (isAborted()) 
			{
				return -2;
			}
			
			int32_t result = -1;
			bool can_write = wait ? m_wr_semaphore.wait(), true : m_wr_semaphore.poll();
			if (can_write) 
			{
				Node cur_node(0, NULL);
				Node new_node(0, data);

				for (;;)
				{
					int32_t cur_write_idx = m_wr;
					int32_t idx = cur_write_idx & (size - 1);

					cur_node.key = cur_write_idx;
					new_node.key = cur_write_idx;

					if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
					{
						atomicIncrement(&m_wr);
						result = idx;
						m_rd_semaphore.signal();
						break;
					}
				}
			} 
			return isAborted() ? -2 : result;
		}

		template <class T, int32_t size> int32_t BlockingQueue<T,size>::pop(T*& data, bool wait)
		{
			int32_t result = -1;
			bool can_read = wait ? m_rd_semaphore.wait(), true : m_rd_semaphore.poll();
			if (can_read) 
			{
				for (;;)
				{
					int cur_read_idx = m_rd;
					int32_t idx = cur_read_idx & (size - 1);

					Node cur_node(cur_read_idx, m_queue[idx].el);
					Node new_node(cur_read_idx + size, NULL);

					if (compareAndExchange64(&m_queue[idx].val, new_node.val, cur_node.val))
					{
						atomicIncrement(&m_rd);
						data = cur_node.el;
						result = idx;
						m_wr_semaphore.signal();
						break;
					}
				}
			}

			return isAborted() ? -2 : result;
		}
	} // ~namespace MT
} // ~namespace Lux
