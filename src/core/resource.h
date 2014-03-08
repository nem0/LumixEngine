#pragma once

#include "core/delegate_list.h"
#include "core/path.h"

namespace Lux
{
	class LUX_CORE_API Resource LUX_ABSTRACT
	{
	public:
		friend class ResourceManagerBase;

		struct State
		{
			enum Value
			{
				EMPTY = 0,
				LOADING,
				READY,
				UNLOADING,
				FAILURE,
			};

			State() : value(EMPTY) { }
			State(Value _value) : value(_value) { }
			State(int32_t _value) : value(_value) { }
			operator Value() const { return (Value)value; }
			uint16_t value;
		};

		typedef DelegateList<void (State)> ObserverCallback;

		State getState() const { return m_state; }

		bool isEmpty()		const { return State::EMPTY		== m_state; }
		bool isLoading()	const { return State::LOADING	== m_state; }
		bool isReady()		const { return State::READY		== m_state; }
		bool isUnloading()	const { return State::LOADING	== m_state; }
		bool isFailure()	const { return State::FAILURE	== m_state; }

		ObserverCallback& getObserverCb() { return m_cb; }

	protected:
		Resource(const Path& path);
		~Resource();

		//events
		void onEmpty(void);
		void onLoading(void);
		void onReady(void);
		void onUnloading(void);
		void onReloading(void);
		void onFailure(void);

		virtual void doLoad(void) = 0;
		virtual void doUnload(void) = 0;
		virtual void doReload(void) = 0;

		uint32_t addRef(void) { return ++m_ref_count; }
		uint32_t remRef(void) { return --m_ref_count; }

	protected:
		Path m_path;

	private:
		uint16_t m_ref_count;
		State m_state;
		ObserverCallback m_cb;
	};
} // ~namespace Lux