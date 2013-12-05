#pragma once

#include "core/lock_free_queue.h"
#include "platform/atomic.h"
#include "platform/event.h"
#include "platform/semaphore.h"

namespace Lux
{
	namespace MT
	{
		template <class T> struct Transaction 
		{
			void setCompleted()		{ m_event->trigger();		}
			bool isCompleted()		{ return m_event->poll();	}
			void waitForCompletion() { return m_event->wait();	}
			void reset()	{ m_event->reset(); }

			T			data;

			Transaction() { m_event = MT::Event::create("Transaction", MT::EventFlags::MANUAL_RESET); }
			MT::Event*	m_event;
		};

		template <class T, int32_t size> class TransactionQueue 
		{
		public:
			TransactionQueue();
			~TransactionQueue();

			T*						alloc(bool wait);
			void					dealoc(T* tr);

			bool					push(T* tr, bool wait = true);
			T*						pop(bool wait = true);

			bool					isAborted();
			void					abort();
			void					abortFromService();
			bool					isEmpty();

		private:

			T*						allocTS(bool wait);
			void					freeTS(T* ptr);

			struct AllocNode
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

				AllocNode()
				{}

				AllocNode(int32_t k, T* v)
					: key(k)
					, el(v)
				{}
			};

			MT::Semaphore*			m_alloc_sema;
			volatile int32_t		m_alloc_ptr;
			volatile int32_t		m_free_ptr;
			AllocNode				m_alloc[size];
			T						m_trans[size];
			LockFreeQueue<T, size>	m_queue;
		};

		template <class T, int32_t size>
		TransactionQueue<T,size>::TransactionQueue()
			: m_alloc_ptr(0)
			, m_free_ptr(0)
		{
			m_alloc_sema = MT::Semaphore::create("TransactionQueue::AllocSema", size, size);
			for (int32_t i = 0; i < size; i++) 
			{
				m_alloc[i].key = i;
				m_alloc[i].el = &m_trans[i];
			}
		}


		template <class T, int32_t size>
		TransactionQueue<T,size>::~TransactionQueue()
		{
			MT::Semaphore::destroy(m_alloc_sema);
		}

		template <class T, int32_t size>
		T* TransactionQueue<T,size>::allocTS(bool wait)
		{
			bool can_write = wait ? m_alloc_sema->wait(), true : m_alloc_sema->poll();
			if (can_write)
			{
				while (true) 
				{
					const int32_t alloc_ptr = m_alloc_ptr;
					int32_t alloc_idx = alloc_ptr & (size - 1);

					AllocNode new_val(alloc_ptr, NULL);
					AllocNode cur_val(alloc_ptr, m_alloc[alloc_idx].el);

					if (compareAndExchange64(&m_alloc[alloc_idx].val, new_val.val, cur_val.val)) 
					{
						atomicIncrement(&m_alloc_ptr);
						return cur_val.el;
					}
				}
			}

			return NULL;
		}

		template <class T, int32_t size>
		void TransactionQueue<T,size>::freeTS(T* ptr)
		{
			AllocNode cur_val(0, NULL);
			AllocNode new_val(0, ptr);

			while(true) 
			{
				const int32_t free_ptr = m_free_ptr;
				int32_t free_idx = free_ptr & (size - 1);

				cur_val.key = free_ptr;
				new_val.key = free_ptr + size;
				if (compareAndExchange64(&m_alloc[free_idx].val, new_val.val, cur_val.val))
				{
					atomicIncrement(&m_free_ptr);
					break;
				}
			}

			m_alloc_sema->signal();
		}

		template <class T, int32_t size>
		T* TransactionQueue<T,size>::alloc(bool wait)
		{
			return allocTS(wait);
		}

		template <class T, int32_t size>
		void TransactionQueue<T,size>::dealoc(T* tr)
		{
			int32_t tid = tr - &m_trans[0];
			ASSERT(tid >= 0 && tid < size);
			freeTS(tr);
		}

		template <class T, int32_t size>
		bool TransactionQueue<T,size>::push(T* tr, bool wait)
		{
			int32_t tid = tr - &m_trans[0];
			ASSERT(tid >= 0 && tid < size);
			return m_queue.push(tr, wait) >= 0;
		}

		template <class T, int32_t size>
		T* TransactionQueue<T,size>::pop(bool wait)
		{
			T* tr = NULL;
			if (m_queue.pop(tr, wait) < 0) {
				return NULL;
			}
			return tr;
		}

		template <class T, int32_t size>
		bool TransactionQueue<T,size>::isAborted()
		{
			return m_queue.isAborted();
		}

		template <class T, int32_t size>
		void TransactionQueue<T,size>::abort()
		{
			m_queue.abort();
		}

		template <class T, int32_t size>
		void TransactionQueue<T,size>::abortFromService()
		{
			m_queue.abortFromService();
		}

		template <class T, int32_t size>
		bool TransactionQueue<T,size>::isEmpty()
		{
			return m_alloc_ptr == m_free_ptr;
		}
	} // ~namespace MT
} // ~namespace Lux
