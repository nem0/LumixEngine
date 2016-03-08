#pragma once

#include "lumix.h"

// base on molecular musings blogpost


namespace Lumix
{

	template <typename T> class Delegate;
	
	template <typename R>
	class Delegate
	{
		private:
			typedef void* InstancePtr;
			typedef R (*InternalFunction)(InstancePtr);
			struct Stub { InstancePtr first; InternalFunction second; };

			template <R (*Function)()>
			static LUMIX_FORCE_INLINE R FunctionStub(InstancePtr)
			{
				return (Function)();
			}

			template <class C, R (C::*Function)()>
			static LUMIX_FORCE_INLINE R ClassMethodStub(InstancePtr instance)
			{
				return (static_cast<C*>(instance)->*Function)();
			}

		public:
			Delegate(void)
			{
				m_stub.first = nullptr;
				m_stub.second = nullptr;
			}

			template <R (*Function)()>
			void bind(void)
			{
				m_stub.first = nullptr;
				m_stub.second = &FunctionStub<Function>;
			}

			template <class C, R (C::*Function)()>
			void bind(C* instance)
			{
				m_stub.first = instance;
				m_stub.second = &ClassMethodStub<C, Function>;
			}

			R invoke() const
			{
				ASSERT(m_stub.second != nullptr);
				return m_stub.second(m_stub.first);
			}

			bool operator ==(const Delegate<R>& rhs)
			{
				return m_stub.first == rhs.m_stub.first && m_stub.second == rhs.m_stub.second;
			}

		private:
			Stub m_stub;
	}; 

	template <typename R, typename A0>
	class Delegate<R (A0)> 
	{
		private:
			typedef void* InstancePtr;
			typedef R (*InternalFunction)(InstancePtr, A0);
			struct Stub { InstancePtr first; InternalFunction second; };

			template <R (*Function)(A0)>
			static LUMIX_FORCE_INLINE R FunctionStub(InstancePtr, A0 a0)
			{
				return (Function)(a0);
			}

			template <class C, R (C::*Function)(A0)>
			static LUMIX_FORCE_INLINE R ClassMethodStub(InstancePtr instance, A0 a0)
			{
				return (static_cast<C*>(instance)->*Function)(a0);
			}

		public:
			Delegate(void)
			{
				m_stub.first = nullptr;
				m_stub.second = nullptr;
			}

			bool isValid()
			{
				return m_stub.second != nullptr;
			}

			template <R (*Function)(A0)>
			void bind(void)
			{
				m_stub.first = nullptr;
				m_stub.second = &FunctionStub<Function>;
			}

			template <class C, R (C::*Function)(A0)>
			void bind(C* instance)
			{
				m_stub.first = instance;
				m_stub.second = &ClassMethodStub<C, Function>;
			}

			R invoke(A0 a0) const
			{
				ASSERT(m_stub.second != nullptr);
				return m_stub.second(m_stub.first, a0);
			}

			bool operator ==(const Delegate<R (A0)>& rhs)
			{
				return m_stub.first == rhs.m_stub.first && m_stub.second == rhs.m_stub.second;
			}

		private:
			Stub m_stub;
	}; 

	
	template <typename R, typename A0, typename A1>
	class Delegate<R (A0, A1)> 
	{
		private:
			typedef void* InstancePtr;
			typedef R (*InternalFunction)(InstancePtr, A0, A1);
			struct Stub { InstancePtr first; InternalFunction second; };

			template <R (*Function)(A0, A1)>
			static LUMIX_FORCE_INLINE R FunctionStub(InstancePtr, A0 a0, A1 a1)
			{
				return (Function)(a0, a1);
			}

			template <class C, R (C::*Function)(A0, A1)>
			static LUMIX_FORCE_INLINE R ClassMethodStub(InstancePtr instance, A0 a0, A1 a1)
			{
				return (static_cast<C*>(instance)->*Function)(a0, a1);
			}

		public:
			Delegate(void)
			{
				m_stub.first = nullptr;
				m_stub.second = nullptr;
			}

			template <R (*Function)(A0, A1)>
			void bind(void)
			{
				m_stub.first = nullptr;
				m_stub.second = &FunctionStub<Function>;
			}

			template <class C, R (C::*Function)(A0, A1)>
			void bind(C* instance)
			{
				m_stub.first = instance;
				m_stub.second = &ClassMethodStub<C, Function>;
			}

			R invoke(A0 a0, A1 a1) const
			{
				ASSERT(m_stub.second != nullptr);
				return m_stub.second(m_stub.first, a0, a1);
			}

			bool operator ==(const Delegate<R (A0, A1)>& rhs)
			{
				return m_stub.first == rhs.m_stub.first && m_stub.second == rhs.m_stub.second;
			}

		private:
			Stub m_stub;
	}; 

	template <typename R, typename A0, typename A1, typename A2>
	class Delegate<R (A0, A1, A2)> 
	{
		private:
			typedef void* InstancePtr;
			typedef R (*InternalFunction)(InstancePtr, A0, A1, A2);
			struct Stub { InstancePtr first; InternalFunction second; };

			template <R (*Function)(A0, A1, A2)>
			static LUMIX_FORCE_INLINE R FunctionStub(InstancePtr, A0 a0, A1 a1, A2 a2)
			{
				return (Function)(a0, a1, a2);
			}

			template <class C, R (C::*Function)(A0, A1, A2)>
			static LUMIX_FORCE_INLINE R ClassMethodStub(InstancePtr instance, A0 a0, A1 a1, A2 a2)
			{
				return (static_cast<C*>(instance)->*Function)(a0, a1, a2);
			}

		public:
			Delegate(void)
			{
				m_stub.first = nullptr;
				m_stub.second = nullptr;
			}

			template <R (*Function)(A0, A1, A2)>
			void bind(void)
			{
				m_stub.first = nullptr;
				m_stub.second = &FunctionStub<Function>;
			}

			template <class C, R (C::*Function)(A0, A1, A2)>
			void bind(C* instance)
			{
				m_stub.first = instance;
				m_stub.second = &ClassMethodStub<C, Function>;
			}

			R invoke(A0 a0, A1 a1, A2 a2) const
			{
				ASSERT(m_stub.second != nullptr);
				return m_stub.second(m_stub.first, a0, a1, a2);
			}

			bool operator ==(const Delegate<R (A0, A1, A2)>& rhs)
			{
				return m_stub.first == rhs.m_stub.first && m_stub.second == rhs.m_stub.second;
			}

		private:
			Stub m_stub;
	};

	template <typename R, typename A0, typename A1, typename A2, typename A3>
	class Delegate<R (A0, A1, A2, A3)> 
	{
		private:
			typedef void* InstancePtr;
			typedef R (*InternalFunction)(InstancePtr, A0, A1, A2, A3);
			struct Stub { InstancePtr first; InternalFunction second; };

			template <R (*Function)(A0, A1, A2, A3)>
			static LUMIX_FORCE_INLINE R FunctionStub(InstancePtr, A0 a0, A1 a1, A2 a2, A3 a3)
			{
				return (Function)(a0, a1, a2, a3);
			}

			template <class C, R (C::*Function)(A0, A1, A2, A3)>
			static LUMIX_FORCE_INLINE R ClassMethodStub(InstancePtr instance, A0 a0, A1 a1, A2 a2, A3 a3)
			{
				return (static_cast<C*>(instance)->*Function)(a0, a1, a2, a3);
			}

		public:
			Delegate(void)
			{
				m_stub.first = nullptr;
				m_stub.second = nullptr;
			}

			template <R (*Function)(A0, A1, A2, A3)>
			void bind(void)
			{
				m_stub.first = nullptr;
				m_stub.second = &FunctionStub<Function>;
			}

			template <class C, R (C::*Function)(A0, A1, A2, A3)>
			void bind(C* instance)
			{
				m_stub.first = instance;
				m_stub.second = &ClassMethodStub<C, Function>;
			}

			R invoke(A0 a0, A1 a1, A2 a2, A3 a3) const
			{
				ASSERT(m_stub.second != nullptr);
				return m_stub.second(m_stub.first, a0, a1, a2, a3);
			}

			bool operator ==(const Delegate<R (A0, A1, A2, A3)>& rhs)
			{
				return m_stub.first == rhs.m_stub.first && m_stub.second == rhs.m_stub.second;
			}

		private:
			Stub m_stub;
	}; 
}


