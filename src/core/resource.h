#pragma once

#include "core/ifile_system_defines.h"
#include "core/delegate_list.h"
#include "core/path.h"

namespace Lux
{
	// forward declarations
	class ResourceManager;

	class LUX_CORE_API Resource abstract
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

		typedef DelegateList<void (uint32_t)> ObserverCallback;

		State getState() const { return m_state; }

		bool isEmpty()		const { return State::EMPTY		== m_state; }
		bool isLoading()	const { return State::LOADING	== m_state; }
		bool isReady()		const { return State::READY		== m_state; }
		bool isUnloading()	const { return State::LOADING	== m_state; }
		bool isFailure()	const { return State::FAILURE	== m_state; }

		ObserverCallback& getObserverCb() { return m_cb; }

		uint32_t size() const { return m_size; }

	protected:
		Resource(const Path& path, ResourceManager& resource_manager);
		~Resource();

		//events
		void onEmpty(void);
		void onLoading(void);
		void onReady(void);
		void onUnloading(void);
		void onReloading(void);
		void onFailure(void);

		void doLoad(void);
		virtual void doUnload(void) = 0;
		virtual FS::ReadCallback getReadCallback(void) = 0;

		uint32_t addRef(void) { return ++m_ref_count; }
		uint32_t remRef(void) { return --m_ref_count; }

		// this method should be called only from method, which parses the file. Otherwise this method
		// should be reimplemented.
		void addDependency(Resource& dependent_resource);
		void removeDependency(Resource& dependent_resource);

		void onStateChanged(uint32_t new_state);
		void decrementDepCount();

	private:
		uint16_t m_ref_count;
		uint16_t m_dep_count;
		State m_state;

	protected:
		Path m_path;
		uint32_t m_size;
		ObserverCallback m_cb;
		ResourceManager& m_resource_manager;
	};
} // ~namespace Lux