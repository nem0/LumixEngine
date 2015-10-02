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
			READY,
			FAILURE,
		};

		typedef DelegateList<void (State, State)> ObserverCallback;

		State getState() const { return m_current_state; }

		bool isEmpty()		const { return State::EMPTY		== m_current_state; }
		bool isReady()		const { return State::READY		== m_current_state; }
		bool isFailure()	const { return State::FAILURE	== m_current_state; }
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

		virtual void onBeforeReady() {}

		void onCreated(State state);
		void doLoad();
		void doUnload();
		virtual void unload(void) = 0;
		virtual bool load(FS::IFile& file) = 0;

		uint32_t addRef(void) { return ++m_ref_count; }
		uint32_t remRef(void) { return --m_ref_count; }

		// this method should be called only from method, which parses the file. Otherwise this method
		// should be reimplemented.
		void addDependency(Resource& dependent_resource);
		void removeDependency(Resource& dependent_resource);

		void onStateChanged(State old_state, State new_state);

	private:
		void fileLoaded(FS::IFile& file, bool success, FS::FileSystem& fs);
		void checkState();

		Resource(const Resource&);
		void operator=(const Resource&);
		uint16_t m_ref_count;
		uint16_t m_empty_dep_count;
		uint16_t m_failed_dep_count;
		State m_current_state;
		State m_desired_state;

	protected:
		Path m_path;
		size_t m_size;
		ObserverCallback m_cb;
		ResourceManager& m_resource_manager;
	};
} // ~namespace Lumix
