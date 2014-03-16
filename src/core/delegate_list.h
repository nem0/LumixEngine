#pragma once


#include "core/delegate.h"
#include "core/array.h"


namespace Lux
{
	template <typename T> class DelegateList;

	template <typename R>
	class DelegateList<R ()>
	{
		public:
			template <typename C, R (C::*Function)()>
			void bind(C* instance)
			{
				Delegate<R> cb;
				cb.bind<C, Function>(instance);
				m_delegates.push(cb);
			}

			template <typename C, R (C::*Function)()>
			void unbind(C* instance)
			{
				Delegate<R> cb;
				cb.bind<C, Function>(instance);
				for(int i = 0; i < m_delegates.size(); ++i)
				{
					if(m_delegates[i] == cb)
					{
						m_delegates.eraseFast(i);
						break;
					}
				}
			}

			void invoke()
			{
				for(int i = 0; i < m_delegates.size(); ++i)
				{
					m_delegates[i].invoke();
				}
			}

		private:
			Array<Delegate<R> > m_delegates;
	};

	template <typename R, typename A0>
	class DelegateList<R (A0)>
	{
		public:
			template <typename C, R (C::*Function)(A0)>
			void bind(C* instance)
			{
				Delegate<R (A0)> cb;
				cb.bind<C, Function>(instance);
				m_delegates.push(cb);
			}

			template <typename C, R (C::*Function)(A0)>
			void unbind(C* instance)
			{
				Delegate<R (A0)> cb;
				cb.bind<C, Function>(instance);
				for(int i = 0; i < m_delegates.size(); ++i)
				{
					if(m_delegates[i] == cb)
					{
						m_delegates.eraseFast(i);
						break;
					}
				}
			}

			void invoke(A0 a0)
			{
				for(int i = 0; i < m_delegates.size(); ++i)
				{
					m_delegates[i].invoke(a0);
				}
			}

		private:
			Array<Delegate<R (A0)> > m_delegates;
	};

	template <typename R, typename A0, typename A1>
	class DelegateList<R (A0, A1)>
	{
		public:
			template <R (Function)(A0, A1)>
			void bind()
			{
				Delegate<R (A0, A1)> cb;
				cb.bind<Function>();
				m_delegates.push(cb);
			}

			template <typename C, R (C::*Function)(A0, A1)>
			void bind(C* instance)
			{
				Delegate<R (A0, A1)> cb;
				cb.bind<C, Function>(instance);
				m_delegates.push(cb);
			}

			template <typename C, R (C::*Function)(A0, A1)>
			void unbind(C* instance)
			{
				Delegate<R (A0, A1)> cb;
				cb.bind<C, Function>(instance);
				for(int i = 0; i < m_delegates.size(); ++i)
				{
					if(m_delegates[i] == cb)
					{
						m_delegates.eraseFast(i);
						break;
					}
				}
			}

			void invoke(A0 a0, A1 a1)
			{
				for(int i = 0; i < m_delegates.size(); ++i)
				{
					m_delegates[i].invoke(a0, a1);
				}
			}

		private:
			Array<Delegate<R (A0, A1)> > m_delegates;
	};
} // ~namespace Lux