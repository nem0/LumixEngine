#pragma once

#include "core/delegate_list.h"
#include "core/path.h"

namespace Lux
{
	class Resource LUX_ABSTRACT
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
				ERROR
			};

			State() : value(EMPTY) { }
			State(Value _value) : value(_value) { }
			State(int32_t _value) : value(_value) { }
			operator Value() const { return (Value)value; }
			uint16_t value;
		};

		typedef DelegateList<void (State)> ObserverCallback;

	protected:
		Resource(const Path& path);
		~Resource();

		//events
		virtual void onEmpty(void);
		virtual void onLoading(void);
		virtual void onReady(void);
		virtual void onUnloading(void);
		virtual void onReloading(void);
		// every resource has to handle error state
		virtual void onError(void) = 0;

		virtual void doLoad(void) = 0;
		virtual void doUnload(void) = 0;
		virtual void doReload(void) = 0;

		uint32_t addRef(void) { return ++m_ref_count; }
		uint32_t remRef(void) { return --m_ref_count; }
	private:
		Path m_path;
		uint16_t m_ref_count;
		State m_state;
		ObserverCallback m_cb;
	};
} // ~namespace Lux