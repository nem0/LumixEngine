#pragma once

#include "core/fs/ifile_system_defines.h"
#include "core/delegate_list.h"
#include "core/path.h"

namespace Lumix
{
	// forward declarations
	class ResourceManager;

	class LUMIX_ENGINE_API Resource
	{
	public:
		friend class ResourceManagerBase;

		enum class State : uint32_t
		{
			EMPTY = 0,
			LOADING,
			READY,
			UNLOADING,
			FAILURE,
		};

		typedef DelegateList<void (State, State)> ObserverCallback;

		State getState() const { return m_state; }

		bool isEmpty()		const { return State::EMPTY		== m_state; }
		bool isLoading()	const { return State::LOADING	== m_state; }
		bool isReady()		const { return State::READY		== m_state; }
		bool isUnloading()	const { return State::UNLOADING	== m_state; }
		bool isFailure()	const { return State::FAILURE	== m_state; }
		uint32_t getRefCount() const { return m_ref_count; }

		template <typename C, void (C::*Function)(State, State)>
		void onLoaded(C* instance)
		{
			m_cb.bind<C, Function>(instance);
			if (isReady())
			{
				(instance->*Function)(State::READY, State::READY);
			}
		}

		ObserverCallback& getObserverCb() { return m_cb; }

		size_t size() const { return m_size; }
		const Path& getPath() const { return m_path; }
		ResourceManager& getResourceManager() { return m_resource_manager; }

	protected:
		Resource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		virtual ~Resource();

		virtual void onReady(void);
		void onEmpty(void);
		void onLoading(void);
		void onUnloading(void);
		void onReloading(void);
		void onFailure(void);

		void doLoad(void);
		virtual void doUnload(void) = 0;
		virtual void loaded(FS::IFile& file, bool success, FS::FileSystem& fs) = 0;

		uint32_t addRef(void) { return ++m_ref_count; }
		uint32_t remRef(void) { return --m_ref_count; }

		// this method should be called only from method, which parses the file. Otherwise this method
		// should be reimplemented.
		void addDependency(Resource& dependent_resource);
		void removeDependency(Resource& dependent_resource);

		void onStateChanged(State old_state, State new_state);
		void incrementDepCount();
		void decrementDepCount();

	private:
		void fileLoaded(FS::IFile& file, bool success, FS::FileSystem& fs);

		Resource(const Resource&);
		void operator=(const Resource&);
		uint16_t m_ref_count;
		uint16_t m_dep_count;
		State m_state;
		bool m_is_waiting_for_file;

	protected:
		Path m_path;
		size_t m_size;
		ObserverCallback m_cb;
		ResourceManager& m_resource_manager;
	};
} // ~namespace Lumix
